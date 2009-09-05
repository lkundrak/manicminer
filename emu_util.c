/* Emu utils v0.1
   This are just a few usefull routines for emulators on unix systems,
   they can be used for games on unix in general.
   Current implement stuff:
   -slowdown routines
   -joystick routines
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include "emu_util.h"

#ifdef HAVE_GETTIMEOFDAY

#define UCLOCKS_PER_SEC 2000

/* Standard UNIX clock() is based on CPU time, not real time.
   Here is a real-time drop in replacement for UNIX systems that have the
   gettimeofday() routine.  This results in much more accurate timing for
   throttled emulation.
*/
clock_t clock()
{
  static long init_sec = 0;
  struct timeval tv;
  
  gettimeofday(&tv, 0);
  if (init_sec == 0)
    init_sec = tv.tv_sec;
  return (((tv.tv_sec - init_sec) * 2000) + (tv.tv_usec / 500));
}

#else 

#if (CLOCKS_PER_SEC < 1000)
/* the resolution of the normal clock() call isn't good enough, unless the
   framerate is a division of 100, so we fake a higher resolution */
#define clock() (clock() << 5)
#define UCLOCKS_PER_SEC (CLOCKS_PER_SEC << 5)
#else
#define UCLOCKS_PER_SEC CLOCKS_PER_SEC
#endif

#endif /* ifdef HAVE_GETTIMEOFDAY */

static clock_t clocks_per_refresh;
static clock_t next_refresh;  /* the time the next refresh should occur */
static clock_t init_time;    /* the time slowdown_init was called */
static clock_t pause_time;   /* the time the emulation was paused */
static clock_t sleep_time;   /* total time the emulation has been inactive */
static int     pause_count;  /* the no of times slowdown_pause has been called,
                                since nested pause - resume pairs can occur */

/* You should call this function before using slowdown, it calculates the 
   number of uclocks per screenrefresh.
   Arguments: frames per second the emulation should be slowed to.
*/
void slowdown_init(int frames_per_sec)
{
  clocks_per_refresh = UCLOCKS_PER_SEC / frames_per_sec;
  init_time = clock();
  next_refresh = init_time + clocks_per_refresh;
  sleep_time = 0;
  pause_count = 0;
}


/* When called once every frame update this function slowsdown the emulation
   to the number of frames / sec given to init_slowdown.
   it's return value can be used to automaticly skip frames, it returns:
   1 if it actually slowed the emulation
   0 if the emulation is too slow.  
   So if this function returns 1 draw the next frame. If it returns 0 skip the
   next frame, to catch up speed. 
*/
int slowdown_slow(void)
{
  clock_t new_time;
  
  do {
   new_time = clock();
   /* Under certain conditions new_time while wrap around before next_refresh.
      To stop infinite looping, we have to check this here ;( */
   if ((new_time < -1073741824) && (next_refresh > 1073741824))
      next_refresh = new_time;

#ifdef HAVE_USLEEP
   /* If the system has a good USLEEP, we can try to sleep the
      time away instead of polling - always nice on a *NIX system.
      We sleep for a tenth of the amount of time remaining for this
      frame - this is an attempt to avoid oversleeping. */
   if (new_time < next_refresh)
     usleep( ((next_refresh - new_time)*(100000/CLOCKS_PER_SEC)));
#endif
  } while (next_refresh > new_time);

  next_refresh += clocks_per_refresh;
    
  if (next_refresh > new_time) return 1;

  /* this codes automagically corrects for pause, and being suspended.
     But it only works if the emulation is capable of running at full speed
     (with or without frameskip). Therefore it's disabled and slowdown_pause
     and slowdown_resume should be used instead */
#if 0
  /* if the emulation is too slow, and behind more then 1 sec, the emulation,
     probably has been suspended without calling slowdown_pause/resume */
  if ((new_time - next_refresh) > UCLOCKS_PER_SEC)
  {
    sleep_time += new_time - next_refresh;
    next_refresh = new_time + clocks_per_refresh;
  }
#endif  

  return 0;
}


/* This function should be called every time the emulation is stopped for
   any reason at all. The slowdown routine's use this function together with
   slowdown_resume to recalibrate there timing.
   This function should be used in conjunction with slowdown_resume.
*/
void slowdown_pause(void)
{
  if (pause_count==0) pause_time = clock();
  pause_count++;
}

/* This function should be called every time the emulation is resumed after
   being stopped for any reason at all. This function should be used in 
   conjunction with slowdown_pause.
*/
void slowdown_resume(void)
{
  clock_t sleeping_time;
  pause_count--;
  if (pause_count==0)
  {
    sleeping_time = clock() - pause_time;
    next_refresh += sleeping_time;
    sleep_time += sleeping_time;
  }
}


/* This function returns the number of seconds the emulation is running.
   Not counting the seconds the emulation has been paused by slowdown_pause.
*/
int slowdown_seconds_running(void)
{
  int seconds_running;
  
  seconds_running = ((clock() - init_time) - sleep_time)  / UCLOCKS_PER_SEC;
  if (seconds_running == 0)
     seconds_running = 1;
  
  return seconds_running;
}


#ifdef JOYSTICK
#include <linux/joystick.h>
int j0_fd=-1;                   /* joystick device  */
int j1_fd=-1;
int j0_max_left, j0_max_right, j0_max_up, j0_max_down;  /* calibration data */
int j1_max_left, j1_max_right, j1_max_up, j1_max_down;
int nr_of_joysticks=0;

/* This function initialises all joystick related stuff,
   it returns the nr of joysticks detected */
int joystick_init(void)
{
  int status;
  struct JS_DATA_TYPE js_data={0,0,0};

  j0_fd = open ("/dev/js0", O_RDONLY);
  if (j0_fd < 0) return 0;
  status = read(j0_fd, &js_data, JS_RETURN);
  if (status != JS_RETURN) 
  {
    close(j0_fd);
    return 0;
  }

  j0_max_left  =js_data.x-100;
  j0_max_right =js_data.x+100;
  j0_max_up    =js_data.y-100;
  j0_max_down  =js_data.y+100;
  nr_of_joysticks=1;

  j1_fd = open ("/dev/js1", O_RDONLY);
  if (j1_fd < 0) return 1;
  status = read(j1_fd, &js_data, JS_RETURN);
  if (status != JS_RETURN) 
  { 
    close(j1_fd);
    return 1;
  }

  j1_max_left  =js_data.x-100;
  j1_max_right =js_data.x+100;
  j1_max_up    =js_data.y-100;
  j1_max_down  =js_data.y+100;
  nr_of_joysticks=2;

  return 2;
}

/* This functions cleans up all joystick related stuff */
void joystick_close(void)
{
  if (j0_fd > -1) close(j0_fd);
  if (j1_fd > -1) close(j1_fd);
}

/* This function reads the current joystick values and stores
   them in the joydata structs which are given. 
   joy0 is mandatory, joy1 is optional and maybe NULL.
   This functions returns:
    0 on success
    1 if reading for joy0 failed
    2 if reading for joy1 failed
    
   The data stored in the structs is only valid when this functions
   returns 0! The call will fail when joy2 is not NULL and only
   one joystick is detected. (although the data in joy0 will still
   make sense) */
int joystick_read(struct joy_data *joy0, struct joy_data *joy1)
{
  int status;
  struct JS_DATA_TYPE js_data={0,0,0};
  
  if (nr_of_joysticks < 1 || joy0 == NULL) return 1;
  if ((status = read (j0_fd, &js_data, JS_RETURN)) != JS_RETURN) return 1;
  memset(joy0, 0, sizeof(struct joy_data));
  
  if (js_data.x < j0_max_left)  j0_max_left  =js_data.x;
  if (js_data.x > j0_max_right) j0_max_right =js_data.x;
  if (js_data.y < j0_max_up)    j0_max_up    =js_data.y;
  if (js_data.y > j0_max_down)  j0_max_down  =js_data.y;
  
  if       (js_data.x < (j0_max_right-j0_max_left)*.25+j0_max_left) joy0->left =1;
   else if (js_data.x > (j0_max_right-j0_max_left)*.75+j0_max_left) joy0->right=1;
  if       (js_data.y < (j0_max_down-j0_max_up)*.25+j0_max_up)      joy0->up   =1;
   else if (js_data.y > (j0_max_down-j0_max_up)*.75+j0_max_up)      joy0->down =1;
   
  if (js_data.buttons & 0x01) joy0->buttons[0] = 1; 
  if (js_data.buttons & 0x02) joy0->buttons[1] = 1; 
  if (js_data.buttons & 0x04) joy0->buttons[2] = 1; 
  if (js_data.buttons & 0x08) joy0->buttons[3] = 1; 
  
  if (joy1 == NULL) return 0;
  if (nr_of_joysticks < 2) return 2;
  if ((status = read (j1_fd, &js_data, JS_RETURN)) != JS_RETURN) return 2;
  memset(joy1, 0, sizeof(struct joy_data));
  
  if (js_data.x < j1_max_left)  j1_max_left  =js_data.x;
  if (js_data.x > j1_max_right) j1_max_right =js_data.x;
  if (js_data.y < j1_max_up)    j1_max_up    =js_data.y;
  if (js_data.y > j1_max_down)  j1_max_down  =js_data.y;
  
  if       (js_data.x < (j1_max_right-j1_max_left)*.25+j1_max_left) joy1->left =1;
   else if (js_data.x > (j1_max_right-j1_max_left)*.75+j1_max_left) joy1->right=1;
  if       (js_data.y < (j1_max_down-j1_max_up)*.25+j1_max_up)      joy1->up   =1;
   else if (js_data.y > (j1_max_down-j1_max_up)*.75+j1_max_up)      joy1->down =1;
   
  if (js_data.buttons & 0x01) joy1->buttons[0] = 1; 
  if (js_data.buttons & 0x02) joy1->buttons[1] = 1; 
  if (js_data.buttons & 0x04) joy1->buttons[2] = 1; 
  if (js_data.buttons & 0x08) joy1->buttons[3] = 1; 
  
  return 0;
}

#endif
