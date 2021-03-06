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

struct pitchshift_dsd {
	PitchShiftCtx_t *ctxR;
	PitchShiftCtx_t *ctxW;
	double pitch;
	double gainin;
	double gainout;
	
	double steppitch;
	double stepgainin;
	double stepgainout;
	
	int framesin;
	int framesproc;
	enum ast_audiohook_direction dir;
	
	struct ast_audiohook ah[1];
};

static const char *app = "PitchShift";
static const char *synopsis = "Adjusts the pitch of your voice";
static const char *desc = ""
						  "  PitchShift(<pitch>,<gainin>,<gainout>,<dir>,<steppitch>,<stepgainin>,<stepgainout>)\n\n"
						  "Specify a pitch in interval 0.5 .. 2  Like <1 for deeper and >1 for higher.\n"
						  "gainin is typicaly around 1.0\n"
						  "gainout is typicaly around 0.05\n"
						  "dir  is R,W,B for read, write or both\n"
						  "<step* > are reals that set cooresponding increments for DTMF based parameter tuning 0 disables tuning\n"
						  "Tuning is done with 1,7 gainin; 2,8 pitch; 3,9 gainout\n"
						  "";
						  
static const char *stop_app = "StopPitchShift";
static const char *stop_synopsis = "Removes pitch shifter";
static const char *stop_desc = ""
							   "  StopPitchShift()\n\n"
							   "Removes the pitch shifting effect from a channel, if there is any.\n"
							   "";
						  
static const char *app_addgo = "PitchShiftAddGainOut";
static const char *addgo_synopsis = "Adds to gainout param of ptich shifting";
static const char *addgo_desc = ""
							  "  PitchShiftAddGainOut(<add>)\n\n"
							  "<add> must be float\n"
							  "";

						  
static const char *app_addgi = "PitchShiftAddGainIn";
static const char *addgi_synopsis = "Adds to gainin param of ptich shifting";
static const char *addgi_desc = ""
								"  PitchShiftAddGainIn(<add>)\n\n"
								"<add> must be float\n"
								"";
							  
static const char *app_addpi = "PitchShiftAddPitch";
static const char *addpi_synopsis = "Adds to pitch param of ptich shifting";
static const char *addpi_desc = ""
								"  PitchShiftAddPitch(<add>)\n\n"
								"<add> must be float\n"
								"";
								
static struct ast_datastore_info dsinfo[1];

static int audio_callback(
	struct ast_audiohook *audiohook,
	struct ast_channel *chan,
	struct ast_frame *frame,
	enum ast_audiohook_direction direction)
{

/*	static int logframeskip =0;
	static int framesin=0;
	static int framesconsider=0;
	static int framesprocess=0;
	framesin++;
	if (logframeskip==11) {
		logframeskip=0;
		ast_log(LOG_DEBUG, "framestat in:%d cons:%d pr:%d\n",framesin,framesconsider,framesprocess);
	} else logframeskip++;*/
	
	
	struct ast_datastore *ds;
	struct pitchshift_dsd *dsd;
	
	if (!audiohook || !chan || !frame ) {
		return 0;
	}
	
	
	ast_channel_lock(chan);
	
	if (!(ds = ast_channel_datastore_find(chan, dsinfo, app)) ||
		!(dsd = (struct pitchshift_dsd *)ds->data) 
	){
		ast_channel_unlock(chan);
		ast_log(LOG_WARNING, "where my data at?\n");
		return 0;
	}
	
	
	if (frame->frametype==AST_FRAME_DTMF_END) {
		
		if (frame->subclass=='1' && dsd->stepgainin>0) {
			dsd->gainin+=dsd->stepgainin;
			if (dsd->ctxR) PitchShift_ChangeGainIn(dsd->ctxR,dsd->gainin);
			if (dsd->ctxW) PitchShift_ChangeGainIn(dsd->ctxW,dsd->gainin);
			ast_log(LOG_DEBUG, "New gainin:%f\n",dsd->gainin);
			ast_channel_unlock(chan);
			return 0;
		}
		if (frame->subclass=='7' && dsd->stepgainin>0) {
			dsd->gainin-=dsd->stepgainin;
			if (dsd->ctxR) PitchShift_ChangeGainIn(dsd->ctxR,dsd->gainin);
			if (dsd->ctxW) PitchShift_ChangeGainIn(dsd->ctxW,dsd->gainin);
			ast_log(LOG_DEBUG, "New gainin:%f\n",dsd->gainin);
			ast_channel_unlock(chan);
			return 0;
		}
		if (frame->subclass=='3' && dsd->stepgainout>0) {
			dsd->gainout+=dsd->stepgainout;
			if (dsd->ctxR) PitchShift_ChangeGainOut(dsd->ctxR,dsd->gainout);
			if (dsd->ctxW) PitchShift_ChangeGainOut(dsd->ctxW,dsd->gainout);
			ast_log(LOG_DEBUG, "New gainout:%f\n",dsd->gainout);
			ast_channel_unlock(chan);
			return 0;
		}
		if (frame->subclass=='9' && dsd->stepgainout>0) {
			dsd->gainout-=dsd->stepgainout;
			if (dsd->ctxR) PitchShift_ChangeGainOut(dsd->ctxR,dsd->gainout);
			if (dsd->ctxW) PitchShift_ChangeGainOut(dsd->ctxW,dsd->gainout);
			ast_log(LOG_DEBUG, "New gainout:%f\n",dsd->gainout);
			ast_channel_unlock(chan);
			return 0;
		}
		
		if (frame->subclass=='2' && dsd->steppitch>0) {
			dsd->pitch+=dsd->steppitch;
			ast_log(LOG_DEBUG, "New pitch:%f\n",dsd->pitch);
			ast_channel_unlock(chan);
			return 0;
		}
		if (frame->subclass=='8' && dsd->steppitch>0) {
			dsd->pitch-=dsd->steppitch;
			ast_log(LOG_DEBUG, "New pitch:%f\n",dsd->pitch);
			ast_channel_unlock(chan);
			return 0;
		}
		ast_channel_unlock(chan);
		return 0;
	}
	
	
	
	if (dsd->pitch==1.0 || dsd->pitch<0.5 || dsd->pitch>2 ) {
		ast_channel_unlock(chan);
		return 0; //pitch disabled at this levels
	}
	
 	if (dsd->dir==AST_AUDIOHOOK_DIRECTION_READ && direction==AST_AUDIOHOOK_DIRECTION_WRITE) {
 		ast_channel_unlock(chan);
 		return 0; //pitch disabled in dis direction
 	}
 	if (dsd->dir==AST_AUDIOHOOK_DIRECTION_WRITE && direction==AST_AUDIOHOOK_DIRECTION_READ) {
 		ast_channel_unlock(chan);
		return 0; //pitch disabled in dis direction
 	}

 	
	if (frame->data.ptr == NULL || frame->samples == 0 || frame->frametype != AST_FRAME_VOICE) {
		ast_channel_unlock(chan);
		ast_log(LOG_WARNING, "got incompatible frame type:%d  sc:%d\n",frame->frametype,frame->subclass);
		return 0;
	}
	PitchShiftCtx_t *ctx=NULL;
	if (direction==AST_AUDIOHOOK_DIRECTION_READ) {
		if (!dsd->ctxR) {
			switch (frame->subclass) {
				case AST_FORMAT_SLINEAR:
					dsd->ctxR=PitchShift_Init(8000,10,16, dsd->gainin,dsd->gainout,S16);
				break;
				case AST_FORMAT_SLINEAR16:
					ast_log(LOG_DEBUG, "Note this frame (dir=READ) is 16kHz\n");
					dsd->ctxR=PitchShift_Init(16000,12,16, dsd->gainin,dsd->gainout,S16);
				break;
				default:
					ast_channel_unlock(chan);
					ast_log(LOG_WARNING, "bad audio type: %s\n", ast_getformatname(frame->subclass));
				return 0;
			}
			if (!dsd->ctxR) {
				ast_channel_unlock(chan);
				ast_log(LOG_WARNING, " Failed to initalizae pitchshifting context! (out of mem?)\n");
				return 0;
			}
		}
		ctx=dsd->ctxR;
	} else if (direction==AST_AUDIOHOOK_DIRECTION_WRITE) {
		if (!dsd->ctxW) {
			switch (frame->subclass) {
				case AST_FORMAT_SLINEAR:
					dsd->ctxW=PitchShift_Init(8000,10,16, dsd->gainin,dsd->gainout,S16);
					break;
				case AST_FORMAT_SLINEAR16:
					ast_log(LOG_DEBUG, "Note this frame (dir=WRITE) is 16kHz\n");
					dsd->ctxW=PitchShift_Init(16000,11,16, dsd->gainin,dsd->gainout,S16);
					break;
				default:
					ast_channel_unlock(chan);
					ast_log(LOG_WARNING, "bad audio type: %s\n", ast_getformatname(frame->subclass));
					return 0;
			}
			if (!dsd->ctxW) {
				ast_channel_unlock(chan);
				ast_log(LOG_WARNING, " Failed to initalizae pitchshifting context! (out of mem?)\n");
				return 0;
			}
		}
		ctx=dsd->ctxW;
	} else {
		ast_channel_unlock(chan);
		ast_log(LOG_DEBUG, " What direction this is? %d\n",direction);
		return 0;
	}
	//PitchShift(dsd->ctx,dsd->pitch,frame->samples*dsd->ctx->bytes_per_sample,  (u_int8_t *)frame->data.ptr,(u_int8_t *)frame->data.ptr);
	
	static int logframeskip =0;
	
	
	if (ctx) {
		/*
		if ((dsd->dir!=AST_AUDIOHOOK_DIRECTION_BOTH &&  logframeskip==22) || logframeskip==44) {
			logframeskip=0;
			ast_log(LOG_DEBUG, "current processing p:%f gi:%f go:%f dir:%d\n",dsd->pitch,dsd->gainin,dsd->gainout,dsd->dir);
		} else logframeskip++;
		*/

		PitchShift(ctx,dsd->pitch,frame->samples<<1,  (u_int8_t *)frame->data.ptr,(u_int8_t *)frame->data.ptr);
	}
	
	ast_channel_unlock(chan);
	return 0;
}

static void pitchshift_ds_free(void *data)
{
	ast_log(LOG_DEBUG, "freed voice changer resources\n");
	struct pitchshift_dsd *dsd;
	if (data) {
		dsd = (struct pitchshift_dsd *)data;
		if (dsd->ctxR) PitchShift_DeInit(dsd->ctxR);
		if (dsd->ctxW) PitchShift_DeInit(dsd->ctxW);
		ast_free(data);
	}
	//ast_log(LOG_DEBUG, "freed voice changer resources_out\n");
}

static int setup_pitchshift(struct ast_channel *chan, float pitch, float gainin, float gainout,enum ast_audiohook_direction dir, float steppitch,float stepgainin,float stepgainout)
{
	struct ast_datastore *ds= ast_channel_datastore_find(chan, dsinfo, app);
	struct pitchshift_dsd *dsd=NULL;
	if (ds) { //allready running just chanege params:
		ast_channel_lock(chan);
		dsd=ds->data;
		dsd->pitch=pitch;
		dsd->gainout=gainout;
		dsd->gainin=gainin;
		dsd->dir=dir;
		dsd->stepgainin=stepgainin;
		dsd->steppitch=steppitch;
		dsd->stepgainout=stepgainout;
		
		if (dsd->ctxR) PitchShift_ChangeGain (dsd->ctxR, gainin,gainout);
		if (dsd->ctxW) PitchShift_ChangeGain (dsd->ctxW, gainin,gainout);
		
		ast_channel_unlock(chan);
		ast_log(LOG_DEBUG, "updating pitch to %f gi:%f  go:%f d:%d\n", pitch,gainin,gainout,dir);
		return 0;
	} else { //init the whole thing:
		ast_log(LOG_DEBUG, "new pitchshifting with %f gi:%f go:%f d:%d\n", pitch,gainin,gainout,dir);

		dsd=ast_calloc(1, sizeof(struct pitchshift_dsd));
		/* create audiohook */
		if (ast_audiohook_init(dsd->ah, AST_AUDIOHOOK_TYPE_MANIPULATE, app)) {
			pitchshift_ds_free(dsd);
			ast_log(LOG_WARNING, "failed to make audiohook\n");
			return -1;
		}
		dsd->pitch=pitch;
		dsd->gainin=gainin;
		dsd->gainout=gainout;
		dsd->dir=dir;

		dsd->stepgainin=stepgainin;
		dsd->steppitch=steppitch;
		dsd->stepgainout=stepgainout;
		
		ast_audiohook_lock(dsd->ah);
		dsd->ah->manipulate_callback = audio_callback;
		ast_set_flag(dsd->ah, AST_AUDIOHOOK_WANTS_DTMF);
		ast_audiohook_unlock(dsd->ah);

		/* glue our data thing to channel */
		ds = ast_datastore_alloc(dsinfo, app);
		ds->data = dsd;
		ast_channel_lock(chan);
		ast_channel_datastore_add(chan, ds);
		ast_channel_unlock(chan);
		
		
		/* glue audiohook to channel */
		if (ast_audiohook_attach(chan, dsd->ah) == -1) {
			ast_channel_lock(chan);
			ast_channel_datastore_remove(chan, ds);
			ast_datastore_free(ds);
			ast_channel_unlock(chan);
			ast_log(LOG_WARNING, "failed to attach hook\n");
			return -1;
		}
		
	}
	
	return 0;
}

static int pitchshift_exec(struct ast_channel *chan, void *data)
{
	ast_log(LOG_DEBUG, "%s(%s) called!\n",app,(char *)data);
	
	AST_DECLARE_APP_ARGS(args,
		 AST_APP_ARG(pitch);
		 AST_APP_ARG(gainin);
		 AST_APP_ARG(gainout);
		 AST_APP_ARG(dir);
		 AST_APP_ARG(steppitch);
		 AST_APP_ARG(stepgainin);
		 AST_APP_ARG(stepgainout);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s() missing arguments\n",app);
		return -1;
	}
	
	char *tmp = ast_strdupa(data);
	
	AST_STANDARD_APP_ARGS(args, tmp);
	
	
	int rc;
	struct ast_module_user *u;
	float pitch=1,gainin=1,gainout=0.05;
	float steppitch=0,stepgainin=0,stepgainout=0;
	
	enum ast_audiohook_direction dir=AST_AUDIOHOOK_DIRECTION_READ;
	if (args.pitch) pitch = strtof(args.pitch, NULL);
	if (args.gainin) gainin = strtof(args.gainin, NULL);
	if (args.gainout) gainout = strtof(args.gainout, NULL);
	if (args.dir) {
		if (*args.dir=='W') dir=AST_AUDIOHOOK_DIRECTION_WRITE;
		else if (*args.dir=='B') dir=AST_AUDIOHOOK_DIRECTION_BOTH;
	}
	if (args.stepgainin) stepgainin = strtof(args.stepgainin, NULL);
	if (args.stepgainout) stepgainout = strtof(args.stepgainout, NULL);
	if (args.steppitch) steppitch = strtof(args.steppitch, NULL);
	
	u = ast_module_user_add(chan);
	rc = setup_pitchshift(chan, pitch,gainin,gainout,dir,steppitch,stepgainin,stepgainout);
	ast_module_user_remove(u);
	return rc;
}

static int remove_pitchshift(struct ast_channel *chan)
{
	struct ast_datastore *ds;
	ast_log(LOG_DEBUG, "Detaching pitchshift from channel...\n");
	ast_channel_lock(chan);
	ds = ast_channel_datastore_find(chan, dsinfo, app);
	if (ds) {
		struct pitchshift_dsd * dsd=ds->data;
		//ast_log(LOG_DEBUG, "removing pitchshift AH from channel...\n");
		ast_audiohook_remove (chan,dsd->ah);
		//ast_log(LOG_DEBUG, "removing pitchshift AH from channel...DONE\n");
		ast_audiohook_destroy (dsd->ah);
		ast_channel_datastore_remove(chan, ds);
		ast_datastore_free(ds);
	}
	ast_channel_unlock(chan);
	return 0;
}


static int  pitchshift_addgo_exec (struct ast_channel *chan, void *data){
	struct ast_datastore *ds;
	float gainoutadd=0;
	if (data) gainoutadd = strtof(data, NULL);
	ast_log(LOG_DEBUG, "%s(%f)\n",app_addgo,gainoutadd);
	if (gainoutadd==0.0) return 0;
	
	ast_channel_lock(chan);
	ds = ast_channel_datastore_find(chan, dsinfo, app);
	if (ds) {
		struct pitchshift_dsd * dsd=ds->data;
		if (dsd) {
			dsd->gainout+=gainoutadd;
			ast_log(LOG_DEBUG, "new gainout %f\n",dsd->gainout);
			if (dsd->ctxR) PitchShift_ChangeGainOut(dsd->ctxR,dsd->gainout);
			if (dsd->ctxW) PitchShift_ChangeGainOut(dsd->ctxW,dsd->gainout);
		}
	}
	ast_channel_unlock(chan);
	return 0;
}


static int  pitchshift_addgi_exec (struct ast_channel *chan, void *data){
	struct ast_datastore *ds;
	float gaininadd=0;
	if (data) gaininadd = strtof(data, NULL);
	ast_log(LOG_DEBUG, "%s(%f)\n",app_addgi,gaininadd);
	if (gaininadd==0.0) return 0;
	
	ast_channel_lock(chan);
	ds = ast_channel_datastore_find(chan, dsinfo, app);
	if (ds) {
		struct pitchshift_dsd * dsd=ds->data;
		if (dsd) {
			dsd->gainin+=gaininadd;
			ast_log(LOG_DEBUG, "new gainin %f\n",dsd->gainin);
			if (dsd->ctxR) PitchShift_ChangeGainIn(dsd->ctxR,dsd->gainin);
			if (dsd->ctxW) PitchShift_ChangeGainIn(dsd->ctxW,dsd->gainin);
		}
	}
	ast_channel_unlock(chan);
	return 0;
}

static int  pitchshift_addpi_exec (struct ast_channel *chan, void *data){
	struct ast_datastore *ds;
	float pitchadd=0;
	if (data) pitchadd = strtof(data, NULL);
	ast_log(LOG_DEBUG, "%s(%f)\n",app_addpi,pitchadd);
	if (pitchadd==0.0) return 0;
	
	ast_channel_lock(chan);
	ds = ast_channel_datastore_find(chan, dsinfo, app);
	if (ds) {
		struct pitchshift_dsd * dsd=ds->data;
		if (dsd) {
			dsd->pitch+=pitchadd;
			ast_log(LOG_DEBUG, "new pitch %f\n",dsd->pitch);
		}
	}
	ast_channel_unlock(chan);
	return 0;
}

static int stop_pitchshift_exec(struct ast_channel *chan, void *data)
{
	int rc;
	struct ast_module_user *u;
	u = ast_module_user_add(chan);
	rc = remove_pitchshift(chan);
	ast_module_user_remove(u);
	return rc;
}

static int unload_module(void)
{
	int res;
	res  = ast_unregister_application(app);
	res |= ast_unregister_application(stop_app);
	res |= ast_unregister_application(app_addgo);
	res |= ast_unregister_application(app_addgi);
	res |= ast_unregister_application(app_addpi);
	ast_module_user_hangup_all();
	return res;
}

static int load_module(void)
{
	int res;
	dsinfo->type = app;
	dsinfo->destroy = pitchshift_ds_free;
	res = ast_register_application(
		app, pitchshift_exec, synopsis, desc);
	res |= ast_register_application(
		stop_app, stop_pitchshift_exec, stop_synopsis, stop_desc);
	res |= ast_register_application(
		app_addgo, pitchshift_addgo_exec, addgo_synopsis, addgo_desc);
	res |= ast_register_application(
		app_addgi, pitchshift_addgi_exec, addgi_synopsis, addgi_desc);
	res |= ast_register_application(
		app_addpi, pitchshift_addpi_exec, addpi_synopsis, addpi_desc);
	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Pitch Shift");
