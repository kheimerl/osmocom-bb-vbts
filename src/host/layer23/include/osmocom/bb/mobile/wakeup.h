#ifndef _WAKEUP_H
#define _WAKEUP_H

/* Timeouts */
#define GSM_WAKEUP_PA_POWERUP_TIME_SEC  1 /* Number of seconds and micro-seconds */
#define GSM_WAKEUP_PA_POWERUP_TIME_USEC 0 /* to wait for PA to powerup */    

/* States. */
#define GSM_WAKEUP_INITIATED    1 /* Wakeup & Camping process initiated. */
#define GSM_WAKEUP_WAIT_FOR_PA  2 /* Waiting for PA to power up. */
#define GSM_WAKEUP_TRYING_FIRST 3 /* Wakeup & Camping; first. */
#define GSM_WAKEUP_TRYING_NEXT  4 /* Try the next ARFCN (BTS) */
#define GSM_WAKEUP_TRYING_LAST  5 /* Wakeup & Camping; last */
#define GSM_WAKEUP_SUCCESSFUL   6 /* Successfully camped on a BTS. */ 
#define GSM_WAKEUP_NULL         7 /* Wakeup not activated. */ 

/* Events. */
#define GSM322_EVENT_WAKEUP_PLMN 1 /* Select PLMN */
#define GSM322_EVENT_WAKEUP_CS   2 /* Select Cell */

/* GSM Wakeup. */
struct gsm_wakeupBTS {

  int state;       /* one of GSM322_WAKEUP_ */
  int curr_arfcn;  /* Index to current BTS's ARFCN */
  int arfcn_count; /* Total number of ARFCN to be tried */
  int *arfcns;     /* Array of ARFCNs to try out */
  int reps;        /* RAACH burst repetitions, to wakeup BTS */

  struct osmocom_ms	  *ms;
  struct osmo_timer_list  timer; /* timeout for PA */
  
};

/* Function headers. */
int wakeup_l1cmd_and_timer (struct osmocom_ms *ms);
int wakeup_allowed(struct osmocom_ms *ms);
int process_wakeup_cmd(struct osmocom_ms *ms, int argc, char **argv);
void wakeup_timer_timeout(void *arg);
int gsm322_wakeup_and_camp(struct osmocom_ms *ms, struct msgb *msg);

#endif /* _WAKEUP_H */
