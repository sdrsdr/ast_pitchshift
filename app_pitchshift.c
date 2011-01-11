/*
 * Voice Changer for Asterisk 1.6+
 * Copyright (C) 2005-2010 J.A. Roberts Tunney
 *
 * J.A. Roberts Tunney <jtunney@lobstertech.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License version 2.0.
 *
 */

#define AST_MODULE "app_pitchshift"

#include <asterisk.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/audiohook.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/lock.h>
#include <asterisk/cli.h>
#include <asterisk/options.h>
#include <asterisk/app.h>
#include <asterisk/linkedlists.h>
#include <asterisk/utils.h>

#include <pitchshift.h>

struct voicechanger {
	PitchShiftCtx_t *ctx;
	struct ast_audiohook ah[1];
};

static const char *app = "PitchShift";
static const char *synopsis = "Adjusts the pitch of your voice";
static const char *desc = ""
						  "  PitchShift(<pitch>,<gain>)\n\n"
						  "Specify a pitch in interval 0.5 .. 2  Like <1 for deeper and >1 for higher.\n"
						  "";
						  
static const char *stop_app = "StopPitchShift";
static const char *stop_synopsis = "Removes pitch shifter";
static const char *stop_desc = ""
							   "  StopPitchShift()\n\n"
							   "Removes the pitch shifting effect from a channel, if there is any.\n"
							   "";

static struct ast_datastore_info dsinfo[1];

static int audio_callback(
	struct ast_audiohook *audiohook,
	struct ast_channel *chan,
	struct ast_frame *frame,
	enum ast_audiohook_direction direction)
{
	struct ast_datastore *ds;
	struct voicechanger *vc;
	void *st;
	
	if (!audiohook || !chan || !frame || direction != AST_AUDIOHOOK_DIRECTION_READ) {
		return 0;
	}
	////just return for now
	return 0;

	ast_channel_lock(chan);
	
	if (!(ds = ast_channel_datastore_find(chan, dsinfo, app)) ||
		!(vc = (struct voicechanger *)ds->data) ||
		!(vc->ctx) 
	){
		ast_channel_unlock(chan);
		ast_log(LOG_WARNING, "where my data at?\n");
		return 0;
	}
		
	if (frame->data.ptr == NULL || frame->samples == 0 || frame->frametype != AST_FRAME_VOICE) {
		ast_channel_unlock(chan);
		ast_log(LOG_WARNING, "got incompatible frame\n");
		return 0;
	}
			
			switch (frame->subclass) {
				case AST_FORMAT_SLINEAR:
					st = vc->st8k;
					break;
				case AST_FORMAT_SLINEAR16:
					st = vc->st16k;
					break;
				default:
					ast_channel_unlock(chan);
					ast_log(LOG_WARNING, "bad audio type: %s\n", ast_getformatname(frame->subclass));
					return 0;
			}
			
			float fbuf[frame->samples];
			//vc_voice_change(st, fbuf, (int16_t *)frame->data.ptr, frame->samples, frame->datalen);
			
			ast_channel_unlock(chan);
			
			return 0;
}

static void voicechanger_free(void *data)
{
	struct voicechanger *vc;
	if (data) {
/*		vc = (struct voicechanger *)data;
		vc_soundtouch_free(vc->st8k); vc->st8k = NULL;
		vc_soundtouch_free(vc->st16k); vc->st16k = NULL;
		ast_audiohook_detach(vc->ah);
		ast_audiohook_destroy(vc->ah);*/
		ast_free(data);
	}
	ast_log(LOG_DEBUG, "freed voice changer resources\n");
}

static int install_vc(struct ast_channel *chan, float pitch, float outgain)
{
	struct ast_datastore *ds=NULL;
	struct voicechanger *vc=NULL;
	law_t law;
	if ((chan->nativeformats & AST_FORMAT_SLINEAR)=AST_FORMAT_SLINEAR) law=S16;
	else if ((chan->nativeformats & AST_FORMAT_ULAW)=AST_FORMAT_ULAW) law=MULAW;
	else if ((chan->nativeformats & AST_FORMAT_ALAW)=AST_FORMAT_ALAW) law=ALAW;
	else {
		ast_log(LOG_ERROR, "Channel native formats (0x%x) can't be handled by pitchshift\n",chan->nativeformats);
		return -1;
	}
	ast_log(LOG_DEBUG, "pitch is %f\n", pitch);
	if (-0.1 < pitch && pitch < 0.1) {
		return 0;
	}
	
	/* create soundtouch object */
	vc = ast_calloc(1, sizeof(struct voicechanger));
	if (!(vc->ctx = PitchShiftInit(8000,10,4, pitch,outgain,law))) {
		ast_log(LOG_ERROR, "failed to initialize context for pitchshift (out of mem?)\n");
		return -1;
	}
		
	/* create audiohook */
	ast_log(LOG_DEBUG, "Creating AudioHook object...\n");
	if (ast_audiohook_init(vc->ah, AST_AUDIOHOOK_TYPE_MANIPULATE, app)) {
		voicechanger_free(vc);
		ast_log(LOG_WARNING, "failed to make audiohook\n");
		return -1;
	}
	
	ast_audiohook_lock(vc->ah);
	vc->ah->manipulate_callback = audio_callback;
	ast_set_flag(vc->ah, AST_AUDIOHOOK_WANTS_DTMF);
	ast_audiohook_unlock(vc->ah);
	
	/* glue audiohook to channel */
	if (ast_audiohook_attach(chan, vc->ah) == -1) {
		voicechanger_free(vc);
		ast_log(LOG_WARNING, "failed to attach hook\n");
		return -1;
	}
		
	/* glue our data thing to channel */
	ds = ast_datastore_alloc(dsinfo, app);
	ds->data = vc;
	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, ds);
	ast_channel_unlock(chan);
	
	return 0;
}

static int voicechanger_exec(struct ast_channel *chan, void *data)
{
	int rc;
	struct ast_module_user *u;
	float pitch;
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "voicechanger() missing argument\n");
		return -1;
	}
	pitch = strtof(data, NULL);
	u = ast_module_user_add(chan);
	rc = install_vc(chan, pitch);
	ast_module_user_remove(u);
	return rc;
}

static int uninstall_vc(struct ast_channel *chan)
{
	struct ast_datastore *ds;
	ast_log(LOG_DEBUG, "Detaching Voice Changer from channel...\n");
	ast_channel_lock(chan);
	ds = ast_channel_datastore_find(chan, dsinfo, app);
	if (ds) {
		ast_channel_datastore_remove(chan, ds);
		ast_datastore_free(ds);
	}
	ast_channel_unlock(chan);
	return 0;
}

static int stop_voicechanger_exec(struct ast_channel *chan, void *data)
{
	int rc;
	struct ast_module_user *u;
	u = ast_module_user_add(chan);
	rc = uninstall_vc(chan);
	ast_module_user_remove(u);
	return rc;
}

static int unload_module(void)
{
	int res;
	res  = ast_unregister_application(app);
	res |= ast_unregister_application(stop_app);
	ast_module_user_hangup_all();
	return res;
}

static int load_module(void)
{
	int res;
	dsinfo->type = app;
	dsinfo->destroy = voicechanger_free;
	res = ast_register_application(
		app, voicechanger_exec, synopsis, desc);
	res |= ast_register_application(
		stop_app, stop_voicechanger_exec, stop_synopsis, stop_desc);
	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Voice Changer");
