/* Functions for VBTS Wakeup */

#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/utils.h>
#include <osmocom/gsm/gsm_utils.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/networks.h>
#include <osmocom/bb/mobile/vty.h>
#include <osmocom/bb/mobile/wakeup.h>
#include <osmocom/bb/mobile/app_mobile.h>
#include <osmocom/bb/mobile/gsm322.h>

#include <l1ctl_proto.h>

/* defined in gsm322.c */
extern void new_a_state(struct gsm322_plmn *, int );
extern void new_c_state(struct gsm322_cellsel *, int);
extern void stop_plmn_timer(struct gsm322_plmn *);
extern void stop_cs_timer(struct gsm322_cellsel *);
extern void stop_any_timer(struct gsm322_cellsel *);

/*
 * wakeup_allowed --
 *
 */

int wakeup_allowed(struct osmocom_ms *ms)
{

        struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm322_plmn    *plmn = &ms->plmn;

	if ( ( plmn->state == GSM322_A0_NULL ||
	       plmn->state == GSM322_ANY_SEARCH ||
	       plmn->state == GSM322_A4_WAIT_FOR_PLMN ||
	       plmn->state == GSM322_A5_HPLMN_SEARCH ) &&

	     (cs->state == GSM322_C0_NULL ||
	      cs->state == GSM322_C6_ANY_CELL_SEL ||
	      cs->state == GSM322_C9_CHOOSE_ANY_CELL ||
	      cs->state == GSM322_CONNECTED_MODE_2 ||
	      cs->state == GSM322_C8_ANY_CELL_RESEL ) ){
	  return 1;
	} else {
	  LOGP(DCS, LOGL_INFO, "BTS wakeup is not allowed in PLMN state: '%s' and CS state: '%s' \n", get_cs_state_name(plmn->state), get_cs_state_name(cs->state));
	  return 0;
	}
}

/*
 * process_wakeup_cmd -- 
 *
 */

int process_wakeup_cmd(struct osmocom_ms *ms, int argc, char **argv)
{
  
        struct gsm_wakeupBTS *wakeupBTS = &ms->wakeupBTS;
  
	int count, i=0;
	int reps=1;
  
	/* All of the followings iff the cell is either in "Camped on any cell"
	 * and/or Waiting for PLMNs to appear. Otherwise, ignore it. 
	 */
	
	if ( !wakeup_allowed(ms) ){
	  return 0;
	}

	count = atoi(argv[1]);
	
	wakeupBTS->arfcns      = calloc(1, sizeof(int)*count);
	wakeupBTS->arfcn_count = count;
	wakeupBTS->curr_arfcn  = 0;
	wakeupBTS->state       = GSM_WAKEUP_INITIATED;
	
	/* Loop to get ARFCN */
	for ( i=0; i<count; i++ )
	  wakeupBTS->arfcns[i] = atoi(argv[i+2]);
  
	if ( (i + 2) < argc ){ /* Process optional reps arg. */
	  reps = atoi(argv[ i + 2]);
	  if (reps <= 0){
	    reps = 1;
	  }
	}

	wakeupBTS->reps = reps;

	wakeup_l1cmd_and_timer(ms);
	
	return 1;
}

/*
 * wakeup_l1cmd_and_timer --
 *
 */

int wakeup_l1cmd_and_timer (struct osmocom_ms *ms) 
{
  
        struct gsm_wakeupBTS *wakeupBTS = &ms->wakeupBTS;
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_cellsel *cs = &ms->cellsel;

	int reps = wakeupBTS->reps;

	/* If we have tried all of the passed parameters (ARFCN), we 
	 * fall back to the conventional GSM camping. This is 
	 * handled in gsm322.c
	 */
	if ( wakeupBTS->state == GSM_WAKEUP_TRYING_LAST ){
	  wakeupBTS->state == GSM_WAKEUP_NULL;
	  return 0;
	}
	
	/* Picks the next ARFCN, send to L1, and initiate the timer. 
	 */

	for (;reps > 0; reps--){
	  l1ctl_tx_wakeup_req(ms, wakeupBTS->arfcns[wakeupBTS->curr_arfcn]);
	}
	
	/* Setup timer. We wait for "x" seconds before initiating camping
	 * operation. If camping fails, we fall back to conventional camping
	 * mechanisim, "normal camping" or "any cell camping". 
	 */
	
	//vty_out(vty, "Starting Wakeup search after %d seconds.\n", 
	//	wakeupBTS->timer.timeout);
	
	/* Set the appropraite fields. */
	wakeupBTS->ms = ms;
	wakeupBTS->state = GSM_WAKEUP_WAIT_FOR_PA;
	wakeupBTS->timer.cb   = wakeup_timer_timeout;
	wakeupBTS->timer.data = wakeupBTS;

	/* Reset the radio, and wait for the PA to turn on
	 */
        if (cs->powerscan) {
                l1ctl_tx_reset_req(ms, L1CTL_RES_T_FULL);
		cs->powerscan = 0;
        }
	
	/* turn off the timers (plmn/cellselection), they will be
	 * started if needed, later on.
	 */
	stop_cs_timer(cs);
	stop_any_timer(cs);
	stop_plmn_timer(plmn);

	/* reset the state */
	new_a_state(plmn, GSM322_A0_NULL);
	new_c_state(cs, GSM322_C0_NULL);

	osmo_timer_schedule(&wakeupBTS->timer, GSM_WAKEUP_PA_POWERUP_TIME_SEC, 0);

	return 1;
}


/*
 * wakeup_time_timeout --
 *
 */

void wakeup_timer_timeout(void *arg)
{
	struct gsm_wakeupBTS *wakeupBTS = arg;
	struct osmocom_ms *ms = wakeupBTS->ms;
	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm_settings *set  = &ms->settings;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *nmsg;

	int mask=0;

	LOGP(DPLMN, LOGL_INFO, "Wakeup, PA power up timer has fired.\n");

	/* Turn on the radio */
        if (!ms->cellsel.powerscan && wakeupBTS->state == GSM_WAKEUP_WAIT_FOR_PA) {
                l1ctl_tx_reset_req(ms, L1CTL_RES_T_FULL);
		ms->cellsel.powerscan = 1;
        }

	if ( wakeupBTS->state ==  GSM_WAKEUP_WAIT_FOR_PA ) {

	  /* Initiate PLMN/CS selection and camping process */
	  
	  /* 1.) Setup the stick bit. */
	  set->stick = 1;
	  set->stick_arfcn = wakeupBTS->arfcns[wakeupBTS->curr_arfcn];

	  /* 2.) Setup proper state. */
	  wakeupBTS->curr_arfcn++;

	  /* State setup. */

	  if ( wakeupBTS->curr_arfcn == 0 ) 
	    wakeupBTS->state = GSM_WAKEUP_TRYING_FIRST;
	  
	  if ( wakeupBTS->curr_arfcn == wakeupBTS->arfcn_count )
	    /* For last ARFCN; if we fail to camp we fall back to the
	     * conventional GSM way of life.
	     */
	    wakeupBTS->state = GSM_WAKEUP_TRYING_LAST;
	  
	  if ( wakeupBTS->curr_arfcn < wakeupBTS->arfcn_count )
	    wakeupBTS->state = GSM_WAKEUP_TRYING_NEXT;

	  /* 3.) Initiate conventional GSM camping process. */
	  /* if there is a registered PLMN, we will try to see if this is our 
	   * candidate BTS. As for future enchancemnet, the PLMN data be 
	   * possibly stored on SIM.
	   */
	  
	  if (subscr->plmn_valid) {
	    
	    /* select the registered PLMN */
	    plmn->mcc = subscr->plmn_mcc;
	    plmn->mnc = subscr->plmn_mnc;

	    LOGP(DSUM, LOGL_INFO, "Start search of last registered PLMN "
		 "(mcc=%s mnc=%s  %s, %s)\n", gsm_print_mcc(plmn->mcc),
		 gsm_print_mnc(plmn->mnc), gsm_get_mcc(plmn->mcc),
		 gsm_get_mnc(plmn->mcc, plmn->mnc));
	    LOGP(DPLMN, LOGL_INFO, "Use RPLMN (mcc=%s mnc=%s  "
		 "%s, %s)\n", gsm_print_mcc(plmn->mcc),
		 gsm_print_mnc(plmn->mnc), gsm_get_mcc(plmn->mcc),
		 gsm_get_mnc(plmn->mcc, plmn->mnc));
	    
	    new_a_state(plmn, GSM322_A1_TRYING_RPLMN);

	    /* indicate New PLMN */
	    nmsg = gsm322_msgb_alloc(GSM322_EVENT_NEW_PLMN);

	    if (!nmsg) {
	      // ??
	    }

	    gsm322_cs_sendmsg(ms, nmsg);
	    
	    return;
	  }
	  
	  /* Lets find the PLMN. */
	  plmn->mcc = plmn->mnc = 0;
	  
	  /* initiate search at cell selection */
	  LOGP(DSUM, LOGL_INFO, "Search for network\n");
	  LOGP(DPLMN, LOGL_INFO, "Switch on, no RPLMN, start PLMN search "
	       "first.\n");
	  
	  nmsg = gsm322_msgb_alloc(GSM322_EVENT_PLMN_SEARCH_START);

	  if (!nmsg) {
	    // ??
	  }

	  gsm322_cs_sendmsg(ms, nmsg);
	}
	
}

/*
 * wakeup_proces_next_arfcn --
 *
 */

int wakeup_process_next_arfcn(struct osmocom_ms *ms)
{

	struct gsm322_plmn *plmn = &ms->plmn;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm_wakeupBTS *wakeupBTS = &ms->wakeupBTS;
	
	/* If we are not processing wakeup cmd, return false. */

	if ( wakeupBTS->state == GSM_WAKEUP_NULL ) {
	  return 0;
	}

	/* If we are processing wakeup cmd; should the next ARFCN be tried? */
	
	if ( wakeupBTS->state != GSM_WAKEUP_TRYING_LAST ) {

	  /*
	   * We only need to look for _ANY_CELL_SEL
	   */

	  if  ( ( plmn->state == GSM322_A5_HPLMN_SEARCH    ||
		  plmn->state == GSM322_A4_WAIT_FOR_PLMN ) ||
		( cs->state == GSM322_C6_ANY_CELL_SEL     ||
		  cs->state == GSM322_C9_CHOOSE_ANY_CELL  ||
		  cs->state == GSM322_ANY_SEARCH       ||
		  cs->state == GSM322_HPLMN_SEARCH ) ){
	    return wakeup_l1cmd_and_timer(ms);
	  } 
	}
 
	return 0;
}

