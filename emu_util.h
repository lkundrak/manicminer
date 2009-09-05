/* Emu utils v0.1
   This are just a few usefull routines for emulators on unix systems,
   they can be used for games on unix in general.
   Current implement stuff:
   -slowdown routines
   -joystick routines
*/

/* configuration part, set the correct defines for your system here or in the 
   makefile.
   valid defines:
   HAVE_GETTIMEOFDAY       -define this if your version of unix supports the
                            gettimeofday() routine, and you want it used for
                            more accurate timing.
   HAVE_USLEEP             -define this if your version of unix supports the
                            usleep() routine, and you want it used for
			    giving away unused CPU time.
   JOYSTICK                -define this if you want i386 style joystick
                            support, don't compile this if you don't
                            have the i386 style joystick driver, otherwise 
                            emu_util won't compile.
*/
/* #define HAVE_GETTIMEOFDAY */
/* #define JOYSTICK */

/* slowdown stuff */
/* prototypes */

/* You should call this function before using slowdown, it calculates the 
   number of uclocks per screenrefresh.
   Arguments: frames per second the emulation should be slowed to.
*/
void slowdown_init(int frames_per_sec);

/* When called once every frame update this function slowsdown the emulation
   to the number of frames / sec given to init_slowdown.
   it's return value can be used to automaticly skip frames, it returns:
   1 if it actually slowed the emulation
   0 if the emulation is too slow.  
   So if this function returns 1 draw the next frame. If it returns 0 skip the
   next frame, to catch up speed. 
*/
int slowdown_slow(void);

/* This function should be called every time the emulation is stopped for
   any reason at all. The slowdown routine's use this function together with
   slowdown_start. To recalibrate there timing.
   This function should be used in conjunction with slowdown_resume.
*/
void slowdown_pause(void);

/* This function should be called every time the emulation is resumed after
   being stopped for any reason at all. This function should be used in 
   conjunction with slowdown_pause.
*/
void slowdown_resume(void);

/* This function returns the number of seconds the emulation is running.
   Not counting the seconds the emulation has been paused by slowdown_pause.
*/
int slowdown_seconds_running(void);


/* joystick stuff */
#ifdef JOYSTICK
/* joystick data struct */

struct joy_data
{
   int left, right;
   int up, down;
   int buttons[4];
   int x, y;
};

/* prototypes */

/* This function initialises all joystick related stuff,
   it returns the nr of joysticks detected */
int joystick_init(void);

/* This functions cleans up all joystick related stuff */
void joystick_close(void);

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
int joystick_read(struct joy_data *joy0, struct joy_data *joy1);

#endif /* #ifdef JOYSTICK */
