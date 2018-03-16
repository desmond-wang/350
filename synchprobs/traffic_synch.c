#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>
#include <clock.h>

/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */


struct cv * cvTab[4];
struct lock * lk;
int carList[4];
int noCarCrossing;
bool broadcasting;

/* static void */
/* log(const char *msg, Direction orig, Direction dest) { */
/* 	time_t sec; */
/* 	uint32_t nsec; */
/* 	gettime(&sec, &nsec); */
/* 	int time = sec * 1000 + nsec / 1000000; */
/* 	kprintf("TRAFFIC %d %d %d %s\n", orig, dest, time, msg); */
/* } */

/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */
void
intersection_sync_init(void)
{
  /* replace this default implementation with your own implementation */

	for (int i = 0; i < 4; i++)
		cvTab[i] = cv_create("");
	lk = lock_create("lk");
	for (int i = 0; i < 4; ++i)
		carList[i] = -1;
	noCarCrossing = 0;
	broadcasting = false;

  /* if (intersectionSem == NULL) { */
  /*   panic("could not create intersection semaphore"); */
  /* } */
  return;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
  sem_destroy(intersectionSem);
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  /* replace this default implementation with your own implementation */
  KASSERT(cvTab != NULL);
  KASSERT(lk != NULL);
  for (int i = 0; i < 4; ++i)
	  cv_destroy(cvTab[i]);
  lock_destroy(lk);
}


/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */
static void insertToList(int origin){
	for(int i = 0; i < 4; ++i){
		if (carList[i] == origin){
			return;
		}
		if(carList[i] == -1){
			carList[i] = origin;
			return;
		}
	}
	KASSERT(false);
}
void
intersection_before_entry(Direction origin, Direction destination) 
{
	KASSERT(cvTab != NULL);
	(void) destination;
	lock_acquire(lk);
	if(carList[0] == -1 && noCarCrossing == 0 && !broadcasting){
		/* kprintf("curCV %d \n", curCV); */
		/* log("charging", origin, destination); */
	}
	else { // if (curCV != origin || carList[0] != -1){
		/* log("waiting", origin, destination); */
		insertToList(origin);
		cv_wait(cvTab[origin],lk);
		broadcasting = false;
		/* log("resuming", origin, destination); */
	}
	/* else { */
	/* 	log("passing", origin, destination); */
	/* } */
	noCarCrossing++;
	/* kprintf("enter %d %d \n", origin, destination); */
	lock_release(lk);


}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{
	KASSERT(cvTab != NULL);
	(void) origin;
	(void) destination;
	lock_acquire(lk);
	noCarCrossing--;
	KASSERT(noCarCrossing >= 0);

	/* log("leaving", origin, destination); */
	/* kprintf("leave %d %d new cv = %d \n", origin, destination,curCV); */
	/* kprintf("  noCarCrossing: %d \n", noCarCrossing); */

	/* kprintf("  car list: %d, %d, %d, %d \n", carList[0], carList[1], carList[2],carList[3]); */

	if(noCarCrossing == 0){
		if(carList[0] != -1) {
			cv_broadcast(cvTab[carList[0]],lk);
			/* kprintf("   broadcast %d", curCV); */
			broadcasting = true;
			for(int i = 1; i < 4; ++i){
				carList[i-1] = carList[i];
			}
			carList[3] = -1;
		}
		/* kprintf("  ---> new car list: %d, %d, %d, %d \n", carList[0], carList[1], carList[2],carList[3]); */
	}	

	lock_release(lk);

	
}
