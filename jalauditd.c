/**
 * @file jalauditd.c This file contains the functions that define an
 * auditd plug-in that interfaces with JALP.
 *
 * @section LICENSE
 *
 * All source code is copyright Tresys Technology and licensed as below.
 *
 * Copyright (c) 2011 Tresys Technology LLC, Columbia, Maryland, USA
 *
 * This software was developed by Tresys Technology LLC
 * with U.S. Government sponsorship.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <libconfig.h>
#include <unistd.h>

#include <libaudit.h>
#include <auparse.h>
#include <syslog.h>
#include <glib.h>
#include <pthread.h>

#include <jalop/jalp_context.h>
#include <jalop/jalp_logger.h>
#include <jalop/jalp_app_metadata.h>
#include <jalop/jalp_logger_metadata.h>

#define UNUSED(x) (void)(x)

#define CONFIG_PATH "/etc/jalauditd/jalauditd.conf"

#define SOCKET "socket"
#define SCHEMAS "schemas"
#define KEYPATH "keypath"
#define CERTPATH "certpath"
#define PRINTSTATS "printstats"
#define PRINTSTATSFREQ "printstatsfreq"
#define QUEUEMAXLENGTH "queuemaxlength"

#define RUN	0
#define STOP	1
#define RELOAD	2
static int status = RUN;

GQueue* event_queue = NULL; 
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t data_in_queue = PTHREAD_COND_INITIALIZER;
pthread_cond_t queue_full = PTHREAD_COND_INITIALIZER;

static int print_stats=0;
static int print_stats_freq=60;
static int queue_max_length=10000;
static unsigned int queue_max_length_seen = 0;

static void sig_handle(int sig)
{
	switch (sig) {
	case SIGTERM:
		status = STOP;
		break;
	case SIGHUP:
		status = RELOAD;
		break;
	default:
		break;
	}
}

static void audit_event_handle(auparse_state_t *au,
			auparse_cb_event_t event_type,
			void *user_data)
{
	UNUSED(user_data);
	int i = 0;
	struct jalp_app_metadata *app_data = NULL;
	struct jalp_logger_metadata *log_data = NULL;
	struct jalp_param *param_list = NULL;
	struct jalp_param *param = NULL;

	if (event_type != AUPARSE_CB_EVENT_READY) {
		return;
	}

	app_data = jalp_app_metadata_create();
	if (!app_data) {
		syslog(LOG_ERR, "failure creating JALP application metadata");
		goto out;
	}

	app_data->type = JALP_METADATA_LOGGER;

	log_data = jalp_logger_metadata_create();
	if (!log_data) {
		syslog(LOG_ERR, "failure creating JALP logger metadata");
		goto out;
	}

	log_data->logger_name = strdup("auditd");
	if (!log_data->logger_name) {
		syslog(LOG_ERR, "failure strduping logger_name");
		goto out;
	}

	log_data->sd = jalp_structured_data_append(NULL, "audit");
	if (!log_data->sd) {
		syslog(LOG_ERR, "failure appending JALP audit structured data");
		goto out;
	}

	app_data->log = log_data;

	while (auparse_goto_record_num(au, i) > 0) {

		do {
			const char *key = auparse_get_field_name(au);
			const char *value = auparse_get_field_str(au);

			param = jalp_param_append(param, key, value);
			if (!param) {
				syslog(LOG_ERR, "failure appending JALP parameter: %s %s", key, value);
				goto out;
			}

			if (!param_list) {
				param_list = param;
			}
		} while (auparse_next_field(au) > 0);

		log_data->sd->param_list = param_list;
		log_data->message = strdup(auparse_get_record_text(au));
		if (!log_data->message) {
			syslog(LOG_ERR, "failure retrieving auparse record text");
			goto out;
		}
		pthread_mutex_lock(&queue_mutex);
		
		while (g_queue_get_length(event_queue) >= (unsigned int)queue_max_length){
			pthread_cond_wait(&queue_full, &queue_mutex);
		}
		g_queue_push_tail(event_queue, (void*)app_data);
		queue_max_length_seen = MAX(g_queue_get_length(event_queue),queue_max_length_seen);
		pthread_mutex_unlock(&queue_mutex);
		pthread_cond_signal(&data_in_queue);

		i++;
	}
	return;
out:
	jalp_app_metadata_destroy(&app_data);
}

static int config_load(config_t *config)
{
	int rc = 0;

	if (!config) {
		rc = -1;
		goto out;
	}

	rc = config_read_file(config, CONFIG_PATH);
	if (rc != CONFIG_TRUE) {
		syslog(LOG_ERR, "failure reading config file, rc: %d, %s, line: %d", rc,
						config_error_text(config),
						config_error_line(config));
		goto out;
	}
#if defined(LIBCONFIG_VER_MAJOR) \
	&& (((LIBCONFIG_VER_MAJOR == 1) && (LIBCONFIG_VER_MINOR >= 4)) \
	|| (LIBCONFIG_VER_MAJOR > 1))
	config_lookup_int(config,PRINTSTATS, &print_stats);
	config_lookup_int(config,PRINTSTATSFREQ, &print_stats_freq);
	config_lookup_int(config,QUEUEMAXLENGTH, &queue_max_length);
#else
	long print_stats_long = print_stats;
	long print_stats_freq_long = print_stats_freq;
	long queue_max_length_long = queue_max_length;
	config_lookup_int(config,PRINTSTATS, &print_stats_long);
	config_lookup_int(config,PRINTSTATSFREQ, &print_stats_freq_long);
	config_lookup_int(config,QUEUEMAXLENGTH, &queue_max_length_long);
	if(print_stats_long > INT_MAX){
		syslog(LOG_ERR, "print_stats in config file is too big.  Using default value");
	}else{
		print_stats = (int)print_stats_long;
	}
	if(print_stats_freq_long > INT_MAX){
		syslog(LOG_ERR, "print_stats_freq in config file is too big.  Using default value");
	}else{
		print_stats_freq = (int)print_stats_freq_long;
	}
	if(queue_max_length_long > INT_MAX){
		syslog(LOG_ERR, "queue_max_length in config file is too big.  Using default value");
	}else{
		queue_max_length = (int)queue_max_length_long;
	}

#endif

out:
	return rc;
}

static int context_init(config_t *config, jalp_context *ctx)
{
	int rc = 0;
	const char *sockpath = NULL;
	const char *schemas = NULL;
	const char *keypath = NULL;
	const char *certpath = NULL;

	if (!config) {
		rc = -1;
		goto out;
	}

	config_lookup_string(config, SOCKET, &sockpath);
	config_lookup_string(config, SCHEMAS, &schemas);
	config_lookup_string(config, KEYPATH, &keypath);
	config_lookup_string(config, CERTPATH, &certpath);

	rc = jalp_context_init(ctx, sockpath, NULL, "auditd", schemas);

	if (rc != JAL_OK) {
		goto out;
	}

	if (keypath != NULL) {
		rc = jalp_context_load_pem_rsa(ctx, keypath, NULL);
		if (rc != JAL_OK) {
			goto out;
		}
	}

	if (certpath != NULL) {
		rc = jalp_context_load_pem_cert(ctx, certpath);
	}

out:
	return rc;
}

static void* log_stats(void* ptr){
	UNUSED(ptr);
	while(1){
		sleep(print_stats_freq);
		syslog(LOG_INFO, "Max queue length seen: %d", queue_max_length_seen);
		syslog(LOG_INFO, "Current queue length: %d", g_queue_get_length(event_queue));
	}
	return NULL;
}

static void* send_messages_to_local_store(void* ctx)
{
	int rc=0;	
	struct jalp_app_metadata *app_data = NULL;
	while(1){
		pthread_mutex_lock(&queue_mutex);
		while(g_queue_is_empty(event_queue)){
			pthread_cond_wait(&data_in_queue, &queue_mutex);
		}

		app_data = (struct jalp_app_metadata*) g_queue_pop_head(event_queue);

		pthread_mutex_unlock(&queue_mutex);
		pthread_cond_signal(&queue_full);

		rc = jalp_log((jalp_context*) ctx, app_data, NULL, 0);
		if (rc != JAL_OK) {
			syslog(LOG_ERR, "failure sending JALP audit message, rc: %d", rc);	
			jalp_app_metadata_destroy(&app_data);
		}
		free(app_data->log->message);
		app_data->log->message = NULL;

		jalp_param_destroy(&(app_data->log->sd->param_list));
		app_data->log->sd->param_list = NULL;
	}
	return NULL;
}

int main(void)
{
	int rc = 0;
	char msg[MAX_AUDIT_MESSAGE_LENGTH+1];
	auparse_state_t *au = NULL;
	jalp_context *ctx = NULL;
	event_queue = g_queue_new();
	config_t config;
	pthread_t send_ls_thread;
	pthread_t print_stats_thread;

	config_init(&config);

	signal(SIGTERM, sig_handle);
	signal(SIGHUP, sig_handle);

	rc = jalp_init();
	if (rc != JAL_OK) {
		syslog(LOG_ERR, "failure initializing JALP, rc: %d", rc);
		goto out;
	}

	au = auparse_init(AUSOURCE_FEED, 0);
	if (!au) {
		rc = -1;
		syslog(LOG_ERR, "failure initializing auparse");
		goto out;
	}

	while (fgets_unlocked(msg, MAX_AUDIT_MESSAGE_LENGTH, stdin) && 
		(status == RUN || status == RELOAD)) {

		if (status == RELOAD || !ctx) {
			syslog(LOG_INFO, "loading config");

			rc = config_load(&config);
			if (rc < 0) {
				syslog(LOG_ERR, "failure reloading config, rc: %d", rc);
				goto out;
			}

			jalp_context_destroy(&ctx);

			ctx = jalp_context_create();
			if (!ctx) {
				rc = -1;
				syslog(LOG_ERR, "failure creating JALP context");
				goto out;
			}

			rc = context_init(&config, ctx);
			if (rc < 0) {
				syslog(LOG_ERR, "failure resetting JALP context, rc: %d", rc);
				goto out;
			}
			config_destroy(&config);
			
			pthread_cancel(print_stats_thread);
			pthread_cancel(send_ls_thread);
			auparse_add_callback(au, audit_event_handle, ctx, NULL);
			pthread_create(&send_ls_thread, NULL, &send_messages_to_local_store, (void*)ctx);
			if(print_stats){
				pthread_create(&print_stats_thread, NULL, &log_stats, NULL);
			}

			status = RUN;
		}

		rc = auparse_feed(au, msg, strnlen(msg, MAX_AUDIT_MESSAGE_LENGTH));
		if (rc) {
			syslog(LOG_ERR, "failure parsing feed, rc: %d", rc);
			goto out;
		}

		if (feof(stdin)) {
			rc = 0;
			break;
		}
	}

	rc = auparse_flush_feed(au);
out:
	jalp_context_destroy(&ctx);
	jalp_shutdown();
	if (au) {
		auparse_destroy(au);
	}
	return rc;
}
