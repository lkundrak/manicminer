/*////////////////////////////////////////////////////////////	
//	
//	Manic Miner PC	
//	
//	8/1997 By Andy Noble	
//	
//	Compile: wcl386 /oneatx /zp4 /5 /fp3 manic.c	
//
// LINUX PORT BY ADAM D. MOSS : adam@gimp.org
//	
/////////////////////////////////////////////////////////////*/	
	
#include <stdio.h>	
#include <stdlib.h>	
#include <math.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef USE_X11
#include "emu_util.h"
#endif


/* ADMNOTE */
#ifdef USE_X11
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/cursorfont.h>
#include <X11/keysymdef.h>
#else
#include <vga.h>
#endif /* USE_X11 */


#ifdef USE_MIKMOD
#include <sys/stat.h>
#include <mikmod.h>
#include <ptform.h>
#endif /* USE_MIKMOD */


#include "mm-keydf.c"

	
/*/////////////////////////////////////////////////////////////	
//	Keyboard Variables	
/////////////////////////////////////////////////////////////*/	
	
typedef	unsigned long	DWORD;	
typedef	unsigned short	WORD;	
typedef	unsigned char	BYTE;	

typedef unsigned int       uint32;
typedef unsigned short int uint16;
typedef unsigned char      uint8;
	
	

/*************************************************************/
/*************************************************************/
/************** X INIT STUFF *********************************/
#ifdef USE_X11

int wwidth = 320;
int wheight = 200;

Colormap cmap;
Display *display;
Window window;
GC gc;
XImage *ximage;
Cursor cursor;

int has_colourmap=0;
int depth=0;
int depth_bytes=0;
int bytesdeep;

static int shmem_flag=1;
static XShmSegmentInfo shminfo1;
static int gXErrorFlag;

XColor pal[256];
XColor xcolor;
void* pix;

#define error(x) fprintf(stderr,"%s\n",x);fflush(stderr);abort

static int HandleXError(dpy, event)
Display *dpy;
XErrorEvent *event;
{
  gXErrorFlag = 1;

  return 0;
}

static void InstallXErrorHandler()
{
  XSetErrorHandler(HandleXError);
  XFlush(display);
}

static void DeInstallXErrorHandler()
{
  XSetErrorHandler(NULL);
  XFlush(display);
}

void init_Xdisplay(char *name)
{
  char *hello = "Manic Miner";
  XSizeHints hint;
  XSetWindowAttributes xswa;
  XColor cursorfg, cursorbg;
  int screen;


  display = XOpenDisplay(name);
  
  if (display == NULL)
    error("Cannot open display.\n");
  
  screen = DefaultScreen(display);

  printf("DefaultDepth: %d, DisplayCells: %d, defaultvisualid: %ld, defaultclass: %d, BYTESDEEP: ",
	 DefaultDepth(display,screen),
	 DisplayCells(display,screen),
	 DefaultVisual(display,screen)->visualid,
	 DefaultVisual(display,screen)->class);

  has_colourmap = (DefaultVisual(display,screen)->class&1);
  depth = DefaultDepth(display,screen);
  depth_bytes = bytesdeep = (int)ceil(((double)depth)/8.0);

  printf("%d \n\n", depth_bytes);

  if ((bytesdeep!=1)&&(bytesdeep!=2)&&(bytesdeep!=4))
    {
      error("!!!WARNING!!!\nThis game probably can't cope with your X server's graphics mode.\nWe like 8, 15/16, or 32-bit modes.\nWe'll carry on regardless.\nIt probably won't work.\n");
    }

  
  hint.width = hint.max_width = hint.min_width = wwidth;
  hint.height = hint.max_height =hint.min_height = wheight;

  hint.flags = PSize|PMinSize|PMaxSize;

  window = XCreateWindow(display, DefaultRootWindow(display), 0,0,
			 hint.width, hint.height, 0,
			 CopyFromParent, InputOutput, CopyFromParent,
			 0, NULL);
  
  XSetWMNormalHints( display, window, &hint );

  cursor = XCreateFontCursor( display, XC_circle );
  cursorfg.red = 65535;
  cursorfg.blue = cursorfg.green = 0;
  cursorbg.red = cursorbg.green = cursorbg.blue = 0;
  XRecolorCursor( display, cursor, &cursorfg, &cursorbg );
  XDefineCursor( display, window, cursor );

  
  xswa.event_mask = KeyPressMask | KeyReleaseMask
    | EnterWindowMask | LeaveWindowMask;
  XSelectInput(display, window, xswa.event_mask);

  XSetStandardProperties (display, window, hello, hello, None, NULL, 0, &hint);
  XMapWindow(display, window);
  
  /* allocate colours */
  gc = DefaultGC(display, screen);
  cmap = DefaultColormap(display, screen);
 
  pix = calloc(1, 256 * bytesdeep);

  if (XShmQueryExtension(display))
    {
      shmem_flag = 1;
      printf("Using SHM.  Neat.\n");
    }
  else
    {
      shmem_flag = 0;
      fprintf(stderr, "Ugh, no SHM.  Never mind.\n");
    }

  XSync(display, False);

  /************** SHM INIT *************************************/
  
  InstallXErrorHandler();

  if (shmem_flag)
    {
      ximage = XShmCreateImage(display, None, depth, ZPixmap, NULL,
			       &shminfo1,
			       wwidth, wheight);

      /* If no go, then revert to normal Xlib calls. */

      if (ximage==NULL)
	{
	  if (ximage!=NULL)
	    XDestroyImage(ximage);
	  fprintf(stderr, "Shared memory error, disabling (Ximage error)\n");
	}

      /* Success here, continue. */

      shminfo1.shmid = shmget(IPC_PRIVATE, 
			      wwidth*wheight*depth_bytes,
			      IPC_CREAT | 0777);

      if (shminfo1.shmid<0)
	{
	  XDestroyImage(ximage);
	  fprintf(stderr, "Shared memory error, disabling (seg id error)\n");
	}

      shminfo1.shmaddr = (char *) shmat(shminfo1.shmid, 0, 0);

      if (shminfo1.shmaddr==((char *) -1))
	{
	  XDestroyImage(ximage);
	  if (shminfo1.shmaddr!=((char *) -1))
	    shmdt(shminfo1.shmaddr);
	  fprintf(stderr, "Shared memory error, disabling (address error)\n");
	}

      ximage->data = shminfo1.shmaddr;
   
      shminfo1.readOnly = False;
      XShmAttach(display, &shminfo1);

      XSync(display, False);

      if (gXErrorFlag)
	{
	  /* Ultimate failure here. */
	  XDestroyImage(ximage);
	  shmdt(shminfo1.shmaddr);
	  fprintf(stderr, "Shared memory error, disabling.\n");
	  gXErrorFlag = 0;
	}
      else
	{
	  shmctl(shminfo1.shmid, IPC_RMID, 0);
	}

      fprintf(stderr, "Sharing memory. [ %d x %d ]\n",wwidth,wheight);
    }

  DeInstallXErrorHandler();

  /********* END OF SHM INIT ***********************************/
  XSync(display, False);
}
#endif
/************** END OF X INIT STUFF **************************/
/*************************************************************/
/*************************************************************/



unsigned long	Old_Isr;	
BYTE		KeyTable[256];	
/*/////////////////////////////////////////////////////////////	
//	Revision Number	
/////////////////////////////////////////////////////////////*/	
BYTE	VERSION[]={"V1.07"};	



/* ADMNOTE: darn case-insensitive DOS C compilers ;) */
#define DrawEUGENE DrawEugene
#define DrawKONG DrawKong
#define DrawWILLY DrawWilly
#define DrawVROBO DrawVRobo
#define cls Cls


/* LINUX SOUND STUFF */
int dsp;
int enable_sound;
#define SOUNDDEVICE "/dev/dsp"
#define SAMPLERATE 44100

/* ADMNOTE: MIDAS STUBS - FIXME */
/*
typedef struct _MIDASsampleSTRUCT
{
  BYTE* data;
  int length;
} MIDASsampleSTRUCT;
typedef MIDASsampleSTRUCT* MIDASsample;
typedef void* MIDASmodule;
typedef void* MIDASmodulePlayHandle;
typedef void* MIDASplayStatus;
void MIDASstartup(void)
{
  int bits = AFMT_U8;
  int stereo = 0;
  int rate = SAMPLERATE;
  int frag=0x00020009;

  if ((dsp=open(SOUNDDEVICE,O_WRONLY))==-1)
    {
      perror(SOUNDDEVICE);
      enable_sound = 0;
      return;
    }

  enable_sound = 1;

  ioctl(dsp,SNDCTL_DSP_SETFRAGMENT, &frag);
  ioctl(dsp,SNDCTL_DSP_RESET);
  ioctl(dsp,SNDCTL_DSP_SAMPLESIZE,&bits);
  ioctl(dsp,SNDCTL_DSP_STEREO,&stereo);
  ioctl(dsp,SNDCTL_DSP_SPEED,&rate);
  ioctl(dsp,SNDCTL_DSP_SYNC);
  ioctl(dsp,SNDCTL_DSP_NONBLOCK);
}
void MIDASconfig(void) {};
void MIDASsaveConfig(char* a) {};
void MIDASloadConfig(char* a) {};
int MIDASgetDisplayRefreshRate(void) {};
int MIDASinit(void) {};
void MIDASsetOption(int a, int b) {};
void* MIDASloadModule(char* a) {};
void* MIDASloadWaveSample(char* a, int b)
{
  MIDASsample newsample;
  struct stat sts;
  FILE* fp;

  newsample = malloc(sizeof(MIDASsampleSTRUCT));

  stat(a, &sts);
  newsample->length = sts.st_size;

  newsample->data = malloc(newsample->length);
  
  fp = fopen(a, "rb");
  fread(newsample->data, newsample->length, 1, fp);
  fclose(fp);

  return(newsample);
}
void MIDASsetTimerCallbacks(int a, int b, char* c, char* d, char* e) {};
void MIDASopenChannels(int a) {};
void MIDASstopModule(void* a) {};
void MIDAScloseChannels(void) {};
void MIDASclose(void) {};
void MIDASfreeModule(void* a) {};
void MIDASfreeSample(void* a)
{
  free(((MIDASsample)a)->data);
  free((MIDASsample)a);
}
void MIDASplaySample(void* a, int b, int c, int d, int e, int f)
{
  if (enable_sound)
    {
      if (d==SAMPLERATE)
	{
	  //int rate = d;
	  //      ioctl(dsp,SNDCTL_DSP_SPEED,&rate);
	  write(dsp, ((MIDASsample)a)->data, ((MIDASsample)a)->length);
	}
      else
	{
	  int i,j,len,destlen;
	  static BYTE* sbuff = NULL;
	  static sbuff_len = 0;

	  len = ((MIDASsample)a)->length;
	  destlen = (len*SAMPLERATE)/d;

	  if (destlen > sbuff_len)
	    {
	      free(sbuff);
	      sbuff = malloc(destlen);
	      sbuff_len = destlen;
	    }

	  for (i=0;i<destlen;i++)
	    sbuff[i] = ((((MIDASsample)a)->data)[(i*len)/destlen]);
	  

	  write(dsp, sbuff, destlen);
	}
    }
}
void* MIDASplayModuleSection(void* a, int b, int c, int d, int e) {};
void* MIDASplayMoleSection(void* a, int b, int c, int d, int e) {};
void MIDASsetMusicVolume(void* a, int b) {};
void MIDASgetPlayStatus(void* a, void* b) {};
#define MIDAS_CALL
#define TRUE (0==0)
#define FALSE (0!=0)
#define MIDAS_OPTION_FILTER_MODE 0
#define MIDAS_FILTER_NONE 0
#define MIDAS_LOOP_NO 0
#define MIDAS_PAN_MIDDLE 0
*/

#define MIDAS_CALL /**/
#define TRUE (0==0)
#define FALSE (0!=0)
#define MIDAS_OPTION_FILTER_MODE 0
#define MIDAS_FILTER_NONE 0
#define MIDAS_LOOP_NO 0
#define MIDAS_PAN_MIDDLE 0
#ifdef USE_MIKMOD
typedef SAMPLE* MIDASsample;
typedef UNIMOD* MIDASmodule;
typedef UNIMOD* MIDASmodulePlayHandle;
#else
typedef char* MIDASsample;
typedef char* MIDASmodule;
typedef char* MIDASmodulePlayHandle;
#endif
void MIDASstartup(void)
{
#ifdef USE_MIKMOD
  md_mixfreq      = 44100;            /* standard mixing freq */
  md_dmabufsize   = 1024;            /* standard dma buf size (max 32000) */
  md_device       = 0;                /* standard device: autodetect */
  md_volume       = 128;              /* driver volume (max 128) */
  md_musicvolume  = 128;              /* music volume (max 128) */
  md_sndfxvolume  = 128;              /* sound effects volume (max 128) */
  md_pansep       = 100;              /* panning separation (0 = mono, 128 = full stereo) */
  md_reverb       = 0;               /* Reverb (max 15) */
  md_mode = DMODE_16BITS |
    DMODE_INTERP |
    DMODE_STEREO |
    DMODE_SOFT_MUSIC |
    DMODE_SOFT_SNDFX;  /* default mixing mode */
  
  MikMod_RegisterAllLoaders();
  MikMod_RegisterAllDrivers();
  MikMod_SetNumVoices(6,6);
#endif
}
void MIDASconfig(void) {};
void MIDASsaveConfig(char* a) {};
void MIDASloadConfig(char* a) {};
int MIDASgetDisplayRefreshRate(void) {};
int MIDASinit(void)
{
#ifdef USE_MIKMOD
  MikMod_Init();
#endif
  return TRUE;
}
void MIDASsetOption(int a, int b) {};
MIDASmodule MIDASloadModule(char* a)
{
  MIDASmodule s;

#ifdef USE_MIKMOD
  s = MikMod_LoadSong(a, 6);
#endif

  return s;
};
MIDASsample MIDASloadWaveSample(char* a, int b)
{
  MIDASsample newsample;
#ifdef USE_MIKMOD
  struct stat sts;
  FILE* fp;

  newsample = calloc(1, sizeof(SAMPLE));

  stat(a, &sts);

  newsample->length = sts.st_size;
  /*  newsample->flags  = SF_UNSIGNED; */
  newsample->speed  = 22050;
  newsample->volume = 63;

  fp = fopen(a, "rb");
  SL_RegisterSample(newsample,MD_SNDFX,fp);
  SL_LoadSamples();

  /*  printf("%p\n",newsample);fflush(stdout); */
#endif
  
  return(newsample);
}
void MIDASsetTimerCallbacks(int a, int b, char* c, char* d, char* e) {};
void MIDASopenChannels(int a) {};
void MIDASstopModule(void* a)
{
#ifdef USE_MIKMOD
  MP_SetPosition(a, 17);
  /*  MikMod_Update(); */
  Player_Stop();
#endif
}
void MIDAScloseChannels(void) {};
void MIDASclose(void) {};
void MIDASfreeModule(MIDASmodule a) {};
void MIDASfreeSample(MIDASsample a)
{
#ifdef USE_MIKMOD
  if (a != NULL)
    {
      MD_SampleUnLoad(a->handle);
      free(a);
    }
#endif
}
void MIDASplaySample(MIDASsample a, int b, int c, int d, int e, int f)
{
#ifdef USE_MIKMOD
  int voice;

  /*  MikMod_Update(); */
  voice = MikMod_PlaySample(a, 0, 0);
  /*  Voice_SetVolume(voice, e); */
  Voice_SetFrequency(voice, d);
  Voice_SetPanning(voice, 128);
  /*  MikMod_Update(); */
#endif
}
MIDASmodule MIDASplayModuleSection(MIDASmodule a, int b, int c, int d, int e)
{
#ifdef USE_MIKMOD
  MP_SetPosition(a, b);

  Player_Start(a);
  /*  MikMod_Update(); */
#endif
  return a;
}
void MIDASsetMusicVolume(MIDASmodule a, int b)
{
  /*  md_musicvolume = (b*127)/64; */
}




/*/////////////////////////////////////////////////////////////	
//	Includes	
/////////////////////////////////////////////////////////////*/
#include	"mm-pal.c"	
	
#include	"mm-map2.c"	
#include	"mm-blocx.c"	
#include	"mm-conv.c"	
#include	"mm-exits.c"	
#include	"mm-keys2.c"	
#include	"mm-tplat.c"	
#include	"mm-air.c"	
#include	"mm-hrobo.c"	
#include	"mm-vrobo.c"	
#include	"mm-final.c"	
#include	"mm-swit.c"	
#include	"mm-font.c"	
#include	"mm-fant.c"	
#include	"mm-over.c"	
	
#include	"mm-eug2.c"	
#include	"mm-kong.c"	
#include	"mm-sky.c"	
#include	"mm-sun.c"	
#include	"mm-fill.c"	
#include	"mm-piano.c"	
#include	"mm-pkeys.c"	
#include	"mm-load.c"	
	
#include	"mm-willy.c"	
#include	"mm-ftsml.c"	
	
#include	"mm-house.c"	
#include	"mm-win.c"	
	
#include	"mm-end.c"	
	
/*/////////////////////////////////////////////////////////////	
//	Globals	
/////////////////////////////////////////////////////////////*/	
	
BYTE	old_video;	
/*ADMNOTE
//BYTE	*screen=(BYTE *)0xa0000;	
//BYTE	screen[64000 * 4]; */
BYTE*   screen = NULL;
#ifdef USE_X11
#else
BYTE*   realscreen = NULL;
#endif
BYTE    page1[64000];
BYTE    page2[64000];
int     pix_offset = 0;
int	yoff[400];	
	
BYTE	page,vpage;	
	
BYTE	bright[]= {0,1,2,3,4,5,6,7,8,7,6,5,4,3,2,1};	
BYTE	bright2[]={0,1,2,3,4,3,2,1,0,1,2,3,4,3,2,1};	
	
#define	KONGPAUSE	8	
	
BYTE	PALwhite[768];	
BYTE	PALblack[768];	
BYTE	PALfade[768];	
BYTE	PALover[768];	
	
/*/////////////////////////////////////////////////////////////	
//	Current Map Info	
/////////////////////////////////////////////////////////////*/	
	
BYTE	cMAP[512];	
BYTE	cCRUMB[512];	
BYTE	cTITLE[33];	
	
BYTE	cBGink;	
BYTE	cBGpaper;	
	
BYTE	cPLAT1ink;	
BYTE	cPLAT1paper;	
	
BYTE	cPLAT2ink;	
BYTE	cPLAT2paper;	
	
BYTE	cWALLink;	
BYTE	cWALLpaper;	
	
BYTE	cCRUMBink;	
BYTE	cCRUMBpaper;	
	
BYTE	cKILL1ink;	
BYTE	cKILL1paper;	
	
BYTE	cKILL2ink;	
BYTE	cKILL2paper;	
	
BYTE	cCONVink;	
BYTE	cCONVpaper;	
	
BYTE	cBORDER;	
	
BYTE	cPLAT1gfx;	
BYTE	cPLAT2gfx;	
BYTE	cWALLgfx;	
BYTE	cCRUMBgfx;	
BYTE	cKILL1gfx;	
BYTE	cKILL2gfx;	
BYTE	cCONVgfx;	
BYTE	cEXITgfx;	
BYTE	cKEYgfx;	
	
WORD	cWILLYx;	
WORD	cWILLYy;	
WORD	cWILLYxold[2];	
WORD	cWILLYyold[2];	
WORD	cWILLYf;	
BYTE	cWILLYd;	
BYTE	cWILLYm;	
BYTE	cWILLYj;	
BYTE	cWILLYbuff[2][256];	
WORD	cWILLYjp[]={4,4,3,3,2,2,1,1,0,0,1,1,2,2,3,3,4,4};	
WORD	cWILLYfall;	
WORD	cWILLYjs;	
	
WORD	cCONVx;	
WORD	cCONVy;	
BYTE	cCONVd;	
BYTE	cCONVl;	
BYTE	cCONVf;	
BYTE	cCONVm;	
	
WORD	cKEYx[5];	
WORD	cKEYy[5];	
BYTE	cKEYb[5];	
BYTE	cKEYs[5];	
	
WORD	cSWITCHx[2];	
WORD	cSWITCHy[2];	
BYTE	cSWITCHs[2];	
	
WORD	cEXITx;	
WORD	cEXITy;	
BYTE	cEXITb;	
BYTE	cEXITm;	
	
BYTE	cAIR;	
BYTE	cAIRp;	
	
BYTE	cHROBOink[4];	
BYTE	cHROBOpaper[4];	
WORD	cHROBOx[4];	
WORD	cHROBOy[4];	
WORD	cHROBOmin[4];	
WORD	cHROBOmax[4];	
BYTE	cHROBOd[4];	
BYTE	cHROBOs[4];	
WORD	cHROBOgfx[4];	
BYTE	cHROBOflip[4];	
BYTE	cHROBOanim[4];	
WORD	cHROBOxold[4][2];	
WORD	cHROBOyold[4][2];	
	
BYTE	cVROBOink[4];	
BYTE	cVROBOpaper[4];	
WORD	cVROBOx[4];	
WORD	cVROBOy[4];	
WORD	cVROBOmin[4];	
WORD	cVROBOmax[4];	
BYTE	cVROBOd[4];	
BYTE	cVROBOs[4];	
WORD	cVROBOgfx[4];	
BYTE	cVROBOanim[4];	
WORD	cVROBOxold[4][2];	
WORD	cVROBOyold[4][2];	
	
/*/////////////////////////////////////////////////////////////	
//	Special Robots	
/////////////////////////////////////////////////////////////*/	
	
WORD	EUGENEx;	
WORD	EUGENEy;	
WORD	EUGENEmin;	
WORD	EUGENEmax;	
BYTE	EUGENEd;	
BYTE	EUGENEm;	
BYTE	EUGENEc;	
WORD	EUGENExold[2];	
WORD	EUGENEyold[2];	
	
BYTE	SWITCH1m;	
BYTE	SWITCH2m;	
	
WORD	HOLEy;	
BYTE	HOLEl;	
	
WORD	KONGx;	
WORD	KONGy;	
WORD	KONGxold[2];	
WORD	KONGyold[2];	
WORD	KONGmax;	
BYTE	KONGm;	
BYTE	KONGc;	
BYTE	KONGf;	
BYTE	KONGp;	
	
WORD	SKYx[3];	
WORD	SKYy[3];	
WORD	SKYmax[3];	
WORD	SKYxold[3][2];	
WORD	SKYyold[3][2];	
BYTE	SKYs[3];	
BYTE	SKYf[3];	
BYTE	SKYm[3];	
BYTE	SKYc[3];	
BYTE	SKYp[3];	
	
BYTE	DEATHm;	
WORD	DEATHc;	
	
WORD	BOOTy;	
	
WORD	SUNy;	
BYTE	SUNm;	
BYTE	SUNh;	
BYTE	SUNbuff[2][384];	
WORD	SUNyold[2];	
BYTE	SUNhold[2];	
	
/*/////////////////////////////////////////////////////////////	
//	Solar Power Generator	
/////////////////////////////////////////////////////////////*/	
	
WORD	SPGx[2][64];	
WORD	SPGy[2][64];	
	
/*/////////////////////////////////////////////////////////////	
//	Game Globals	
/////////////////////////////////////////////////////////////*/	
	
unsigned long	SCORE;	
unsigned long	HISCORE=0;	
unsigned long	EXTRA;	
unsigned long	EXTRAdelta=10000;	
BYTE		EXTRAm;	
BYTE		EXTRAc;	
	
WORD	LIVES;	
BYTE	LIVESf;	
BYTE	LIVESp;	
BYTE	LEVEL=0;	
	
BYTE	MUSICon=1;	
	
BYTE	old=0;	
	
BYTE	SPEED=2;	
	
BYTE	MODE=3;	
BYTE	GAMEm=0;	
	
BYTE	DEMOm=0;	
BYTE	DEMOp;	
	
BYTE	TITLEm=0;	
BYTE	TITLEwf;	
BYTE	TITLEwp;	
	
BYTE	OVERm=0;	
BYTE	OVERink;	
BYTE	OVERp;	
	
BYTE	INK=7;	
BYTE	PAPER=0;	
	
BYTE	CHEAT=0;	
BYTE	CHEATp=0;	
BYTE	CHEATh;	
BYTE	CHEATkey[]={9,11,3,10,3,7};	
	
BYTE	PAUSE;	
BYTE	LASTm;	
WORD	LASTc;	
BYTE	LASTp;	
WORD	WINDOW[]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};	
	
BYTE	TEXTm;	
WORD	TEXTpause;	
BYTE	TEXTpoint;	
BYTE	TEXTink;	
BYTE	TEXTfade;	
	
/*///////////////////////////////////////////////////////////*/	
	
BYTE	PIANOkhit[32];	
BYTE	PIANOkey[]={0,1,1,2,0,1,2,0,1,1,2,0,1,2,0,1,2,0,1,1,2,0,1,2,0,1,1,2,0,1,2,3};	
WORD	PIANOc;	
BYTE	PIANObodge1;	
BYTE	PIANObodge2;	
BYTE	PIANObodgep;	
BYTE	PIANObodgep2;	
	
BYTE	SCROLLbuff[]="                                 ";	
WORD	SCROLLpos;	
BYTE	PIXELbuff[264*8];	
WORD	PIXELoff;	
	
BYTE	LOADm;	
BYTE	LOADp;	
	
BYTE	PREFSm;	
BYTE	PREFSh1;	
BYTE	PREFSh2;	
BYTE	PREFSh3;	
	
BYTE	CHAN[]={0,1,2,8,9,10,16,17,18};	
	
	
/*/////////////////////////////////////////////////////////////	
//	Tales from a Parallel Universe	
/////////////////////////////////////////////////////////////*/	
	
BYTE	TONKS=0;	
	
/*/////////////////////////////////////////////////////////////	
//	Audio Stuff	
/////////////////////////////////////////////////////////////*/	
#define	INGAMEpc	0x00	
#define	INGAMEspec	0x09	
#define	TITLEmusic	0x0d	
#define	OPTIONSmusic	0x12	
#define	FINALcavern	0x16	
#define	ENDsequence	0x20	
	
BYTE	MUSICtype=0;	
BYTE	VOL=64;	
	
BYTE	MUSICh;	




/*ADMNOTE: we manually handle timers on UNIX
//WORD	volatile	FrCt=0;	*/
WORD    FrCt=0;
/* 60 Hz */
#define FHZ 60
#define microsec_per_frame 16666
WORD    UpdateFrCt(void)
{
  static int firsttime = TRUE;
  static long long first_timer_ctr;
  static long long this_timer_ctr;
  static int this_frnum;
  static int last_frnum;
  struct timeval tv;

  if (firsttime)
    {
      gettimeofday(&tv, NULL);
      first_timer_ctr = (tv.tv_sec*1000000)+tv.tv_usec;
      firsttime = FALSE;
      last_frnum = 0;
    }

  gettimeofday(&tv, NULL);
  this_timer_ctr = (tv.tv_sec*1000000)+tv.tv_usec;

  this_frnum = (this_timer_ctr - first_timer_ctr) / microsec_per_frame;

  FrCt += (this_frnum - last_frnum);

  last_frnum = this_frnum;

  return(FrCt);
}


WORD	NEXTBIT=0;	
DWORD	REFRESH;	
MIDASsample	wav;	
MIDASsample	die;	
MIDASsample	pick;	
MIDASmodule	mod;	
MIDASmodulePlayHandle	modon=0;	
	
BYTE	FORCE;	
/*/////////////////////////////////////////////////////////////	
//	
/////////////////////////////////////////////////////////////*/	
	
void MIDAS_CALL	PREvr(void);	
	
/*/////////////////////////////////////////////////////////////	
// Main Code.	
/////////////////////////////////////////////////////////////*/	
int main(int argc, BYTE *argv[])	
{	
	int	quit=0;	
	
#ifdef USE_X11
	init_Xdisplay(NULL);

	slowdown_init(FHZ);
#else
	/*ADMNOTE: set up SVGALIB */
	vga_init();
	vga_setmode(5);
	keyboard_init();
	keyboard_setdefaulteventhandler();
	realscreen=(BYTE*) vga_getgraphmem();
#endif

	pix_offset = 33 + 4*320;  /* for screen centering in this mode */


	if(argc==2)	
	{	
		FORCE=1;	
	}	
	else	
	{	
		FORCE=0;	
	}	
	
	MIDASstartup();	
	
	LoadInfo();	
	
	SetupSound();	
	
	GetVideoMode( &old_video );	

	screen = page1;
	
	/*-------------------------------------------------------------	*/
	
	REFRESH=MIDASgetDisplayRefreshRate();	
	if(REFRESH==0)	
		REFRESH=59450;	
	
	MIDASsetOption(MIDAS_OPTION_FILTER_MODE,MIDAS_FILTER_NONE);	
	
	if(MIDASinit()==FALSE)	
	{	
		return(0);	
	}	
	
	mod=MIDASloadModule("mm-data1.dat");	
	//ADMNOTE: filenames changed, now not M$ wav
	wav=MIDASloadWaveSample("mm-data2.raw",MIDAS_LOOP_NO);	
	die=MIDASloadWaveSample("mm-data3.raw",MIDAS_LOOP_NO);	
	pick=MIDASloadWaveSample("mm-data4.raw",MIDAS_LOOP_NO);
	/*	wav=MIDASloadWaveSample("mm-data2.dat",MIDAS_LOOP_NO);	
	die=MIDASloadWaveSample("mm-data3.dat",MIDAS_LOOP_NO);	
	pick=MIDASloadWaveSample("mm-data4.dat",MIDAS_LOOP_NO);*/
	
	MIDASsetTimerCallbacks(REFRESH,TRUE,&PREvr,NULL,NULL);	
	MIDASopenChannels(9);	
	
	/*-------------------------------------------------------------	*/
	
	SetKeyboard();	
	
	Cls(0);	
	WaitVR();	
	PaletteSet(PALblack);	
	
	WaitVR();	
	Cls(0);	
	SwapPage();	
	
	WaitVR();	
	PaletteSet(PALmain);	
	Cls(0);	
	SwapPage();	
	
	LOADm=0;	
	
	do	
	{	
	
	
		switch(MODE)	
		{	
			case	0://TITLES	
				Titles();	
				break;	
			case	1://DEMO	
				DoDemo();	
				break;	
			case	2://GAME	
				DoGame();	
				break;	
			case	3://LOADING	
				DoLoading();	
				break;	
		}	
	
	
		if( KeyTable[key_f10]==1 )	
			quit=1;	
	
	}	
	while( quit==0 );	
	
	/*-------------------------------------------------------------	*/
	if(modon!=0)	
		MIDASstopModule(modon);	
	
	MIDAScloseChannels();	
	MIDASfreeModule(mod);	
	MIDASfreeSample(wav);	
	MIDASfreeSample(die);	
	MIDASfreeSample(pick);	
	MIDASclose();	
	
	/*-------------------------------------------------------------	*/
	
	WaitVR2();	
	PaletteSet(PALblack);	
	
	WaitVR2();	
	SetVideoMode(old_video);	
	
	SaveInfo();	
		
	printf("Manic Miner PC %s.\n",VERSION);	
	printf("Andy Noble (andy@andyn.demon.co.uk) 8/1997 - DOS version\n");
	printf("Adam D. Moss (adam@foxbox.org) 12/1997 - UNIX port\n");
	
	/*printf("Refresh=%d\n\n",REFRESH);	*/

	/*ADMNOTE - vga/kbd stuff*/
#ifdef USE_X11
#else
	keyboard_close();
	vga_setmode(TEXT);
#endif

	
	return(0);	
}	

/*/////////////////////////////////////////////////////////////	
//	Restore Bios Keyboard Interupt	
/////////////////////////////////////////////////////////////*/	
void RemoveKeyboard( void )	
{	
}	
	
/*/////////////////////////////////////////////////////////////	
//	Set Our Keyboard Interupt	
/////////////////////////////////////////////////////////////*/	
void SetKeyboard( void )	
{	
	
	memset( KeyTable, (BYTE)0, 256 );	
	
}	


/*/////////////////////////////////////////////////////////////	
//	Pressed any key ?
/////////////////////////////////////////////////////////////*/

int
AnyKeyx
(void)
{

  int count;	
  
  /* ADMNOTE */
  /* if (random()%3000 == 0) return (1);*/
  
  for( count=0; count<128; count++ )	
    {	
      if(KeyTable[count] != 0 )	
	return(1);	
    }	
  return(0);	
}	


///////////////////////////////////////////////////////////////	
//	Flush our Keyboard Buffer	
///////////////////////////////////////////////////////////////	
void	FlushKey( void )	
{	
	memset( KeyTable, (BYTE)0, 256 );	
}	
	
///////////////////////////////////////////////////////////////	
// Set Screen Mode.	
///////////////////////////////////////////////////////////////	
void SetVideoMode(int mode)	
{	

}	
	
///////////////////////////////////////////////////////////////	
// Get Screen Mode.	
///////////////////////////////////////////////////////////////	
void GetVideoMode( BYTE *vidmode )	
{	

}	

	
/*/////////////////////////////////////////////////////////////	
// Wait Vertical Retrace	
/////////////////////////////////////////////////////////////*/
void WaitVR( void )	
{	
#ifdef USE_X11
#else
	WORD	old;	
	
	/*ADMNOTE*/
	old=UpdateFrCt();     
#endif
	
#ifdef USE_MIKMOD
        MikMod_Update();
#endif	

#ifdef USE_X11
	/*putchar('1'+slowdown_slow());*/
	XFlush(display);
	slowdown_slow();
#else
	while(old==UpdateFrCt()) {};
#endif

	/*ADMNOTE: we throw in a keyboard check here! */
	DoKeymap();

	/*old=FrCt;	
	while(old==FrCt);*/
	
}	
	
/*/////////////////////////////////////////////////////////////	
// Wait Vertical Retrace	
/////////////////////////////////////////////////////////////*/	
void WaitVR2( void )	
{	
  DoKeymap();
 
#ifdef USE_MIKMOD
  MikMod_Update();
#endif

  WaitVR3();
 

#ifdef USE_X11

#ifdef USE_MIKMOD
  MikMod_Update();
#endif
  WaitVR3();
#ifdef USE_MIKMOD
  MikMod_Update();
#endif
  WaitVR3();

#else
  vga_waitretrace();
#endif

}	
	


/*/////////////////////////////////////////////////////////////	
//	ADMNOTE: UNIX: Update keyboard state
/////////////////////////////////////////////////////////////*/	
int DoKeymap ( void )	
{
#ifdef USE_X11

  KeySym ks = 0;
  int kstat;
  XEvent event;
  char buf[21];

  while (XPending(display))
    {
      XNextEvent(display, &event);

      switch (event.type)
	{
	case KeyPress:
	  kstat = 1;
	case KeyRelease:
	  if (event.type == KeyRelease)
	    kstat = 0;

	  XLookupString(&event.xkey, buf, 20, &ks, NULL);
	  
	  switch (ks)
	    {
	    case 0xFF0D : KeyTable[key_return] = kstat; break;
	    case 0xFF52 :
	    case 0xFF97 :
	    case ' '    : KeyTable[key_space]  = kstat; break;
	    case 0xFF51 :
	    case 'O'    :
	    case 'o'    : KeyTable[key_o]      = kstat; break;
	    case 0xFF53 :
	    case 'P'    :
	    case 'p'    : KeyTable[key_p]      = kstat; break;
	    case 0xFF1B : KeyTable[key_esc]    = kstat; break;
	    case 0xFF91 :
	    case 0xFFBE : KeyTable[key_f1]     = kstat; break;
	    case 0xFF92 :
	    case 0xFFBF : KeyTable[key_f2]     = kstat; break;
	    case 0xFF93 :
	    case 0xFFC0 : KeyTable[key_f3]     = kstat; break;
	    case 0xFF94 :
	    case 0xFFC1 : KeyTable[key_f4]     = kstat; break;
	    case 0xFFC7 : KeyTable[key_f10]    = kstat; break;
	    default: break;
	    }

	  break;
	default: break;
	}
    }

#else
  if (keyboard_update()==0) return;

  /* Otherwise, something changed with the keyboard...
  // ah, sod it... */

  memcpy(KeyTable, keyboard_getstate(), 256);
#endif
}


/*
 * PHYSICALLY DISPLAY 'SCREEN'
 */
void showscreen(void)
{
#ifdef USE_X11
  if (shmem_flag)
    {
      XShmPutImage(display, window, gc, ximage,
		   0,0,
		   0,0,
		   wwidth, wheight, False);
    }
  else
    {
      XPutImage(display, window, gc, ximage,
		   0,0,
		   0,0,
		   wwidth, wheight);
    }
  XSync(display, False);
#else
  memcpy(realscreen, screen, 64000);
#endif
}


/*/////////////////////////////////////////////////////////////	
//	Swap Pages	
/////////////////////////////////////////////////////////////*/	
void	SwapPage( void )	
{	
	WORD	data;	
	BYTE	temp;	
	
	/* ADMNOTE */
	page++;	
	page&=1;	
	vpage++;	
	vpage&=1;	
	WaitVR();	
	showscreen();
	screen = (page==0 ? page1 : page2);
	old++;	
	old&=1;	
	return;
}	
/*/////////////////////////////////////////////////////////////	
//	Swap Pages	
/////////////////////////////////////////////////////////////*/	
void	SwapPage2( void)	
{	
	WORD	data;	
	BYTE	temp;	
	
	/*ADMNOTE */
	page++;	
	page&=1;	
	vpage++;	
	vpage&=1;	
	WaitVR3();
	showscreen();
	screen = (page==0 ? page1 : page2);
	old++;	
	old&=1;	
	return;
}	

/*/////////////////////////////////////////////////////////////	
// Wait Vertical Retrace	
/////////////////////////////////////////////////////////////*/	
void WaitVR3( void )	
{	
	WORD	old;	
	
	/* ADMNOTE:      */
	
#ifdef USE_MIKMOD
	MikMod_Update();
#endif

#ifdef USE_X11
	/*putchar('1'+slowdown_slow());	*/
	XFlush(display);
	slowdown_slow();
#else
	while(NEXTBIT>=UpdateFrCt()) {};
#endif
	FrCt=0;
	DoKeymap();

	/*while(NEXTBIT>=FrCt);	
	FrCt=0;	*/
}	
	
/*/////////////////////////////////////////////////////////////	
// Plot a Pixel to the Screen	
/////////////////////////////////////////////////////////////*/	

void PlotPixel( int x, int y, BYTE colour )
{
  int offset = 320*y + x + pix_offset;

  screen[offset] = colour;

#ifdef USE_X11
  switch (bytesdeep)
    {
    case 1:
      ((uint8 *)ximage->data)[offset] = ((uint8 *)pix)[colour]; break;
    case 2:
      ((uint16 *)ximage->data)[offset] = ((uint16 *)pix)[colour]; break;
    default:
      ((uint32 *)ximage->data)[offset] = ((uint32 *)pix)[colour]; break;      
    }
#endif
}

/*/////////////////////////////////////////////////////////////	
// Get a Pixel from the Screen	
/////////////////////////////////////////////////////////////*/	
char GetPixel( int x, int y )
{
  return (screen[320*y + x + pix_offset]);
}

/*/////////////////////////////////////////////////////////////	
//	Clear Current Page	
/////////////////////////////////////////////////////////////*/	
void Cls( BYTE col )
{
  int pcol, i;
  uint16* u16p;
  uint32* u32p;

  memset(screen, col, 64000);

#ifdef USE_X11
  switch (bytesdeep)
    {
    case 1:
      pcol = ((uint8 *)pix)[col];
      memset((uint8 *)ximage->data, pcol, wwidth*wheight);
      break;
    case 2:
      pcol = ((uint16 *)pix)[col];
      u16p = (uint16 *)ximage->data;
      for (i=0;i<wwidth*wheight;i++)
	{
	  *u16p++ = pcol;
	}
      break;
    default:
      pcol = ((uint32 *)pix)[col];
      u32p = (uint32 *)ximage->data;
      for (i=0;i<wwidth*wheight;i++)
	{
	  *u32p++ = pcol;
	}
      break;
    }
#endif
}

	
/*/////////////////////////////////////////////////////////////	
//	Set Palette1 with Palette2	
/////////////////////////////////////////////////////////////*/	
	
void PaletteFill( char *pal1, char *pal2 )	
{	
	int	count;	
	
	for(count=0;count<768;count++)	
		pal1[count]=pal2[count];	
}	
	
/*/////////////////////////////////////////////////////////////	
//	Set palette	
/////////////////////////////////////////////////////////////*/	
	
void PaletteSet( char *pal1 )	
{	
	int	count;	

#ifdef USE_X11
	static uint8 *lastpal = NULL;

	if (lastpal == NULL)
	  {
	    lastpal = calloc(1, 256*3);
	  }

	putchar('p');fflush(stdout);

	for (count=0; count<256; count++)
	  {
	    if (
		(lastpal[count*3+0] != pal1[count*3+0]) ||
		(lastpal[count*3+1] != pal1[count*3+1]) ||
		(lastpal[count*3+2] != pal1[count*3+2])
		)
	      {
		pal[count].red   = (pal1[count*3+0] * 65535) / 63 ;
		pal[count].green = (pal1[count*3+1] * 65535) / 63 ;
		pal[count].blue  = (pal1[count*3+2] * 65535) / 63 ;
		pal[count].flags|=DoRed|DoGreen|DoBlue;
		
		if ( XAllocColor(display, cmap, &pal[count]) == 0 )
		  printf("** XAllocColor FAILED **\n");
		
		switch (bytesdeep)
		  {
		  case 1: ((uint8 *)pix)[count] =
			    (uint8) pal[count].pixel; break;
		  case 2: ((uint16 *)pix)[count] =
			    (uint16) pal[count].pixel; break;
		  default: ((uint32 *)pix)[count] =
			     (uint32) pal[count].pixel; break;
		  }
	      }
	  }
	memcpy(lastpal, pal1, 256*3);

#else

	for (count=0;count<256;count++)
	  vga_setpalette(count,pal1[count*3],
			 pal1[count*3 +1],
			 pal1[count*3 +2]);

#endif
}	
	
/*/////////////////////////////////////////////////////////////	
//	Fade Palette	
/////////////////////////////////////////////////////////////*/	
	
int PaletteFade( unsigned char *pal1, unsigned char *pal2 )	
{	
	int	count,changed;	
	
	changed=0;	
	
	for(count=0;count<768;count++)	
	{	
	
		if(*pal1 != *pal2)	
		{	
			if(*pal1 > *pal2)	
			{	
				*pal2+=1;	
				changed=1;	
			}	
			else	
			{	
				*pal2-=1;	
				changed=1;	
			}	
		}	
	
		pal1++;	
		pal2++;	
	
	}	
	return(changed);	
}	

/*/////////////////////////////////////////////////////////////	
//	Print Text	
/////////////////////////////////////////////////////////////*/	
void	FontPrint( int xpos, int ypos, BYTE *text )	
{	
	int	count,count2,count3;	
	int	currentx,currenty;	
	int	alpha;	
	BYTE	*fonty;	
	
	count=0;	
	
	xpos*=8;	
	ypos*=8;	
	
	currentx=xpos;	
	currenty=ypos;	
	
	while( *text != 0 )	
	{	
		alpha=(int)*text;	
		text++;	
	
		if(alpha==170) /*="|"	*/
		{	
			currentx=0;	
			currenty+=8;	
			alpha=(int)*text;	
			text++;	
		}	
	
	
		alpha-=32;	
	
		if(alpha>96)	
			alpha-=32;	
	
		alpha*=64;	
		count++;	
		if(count==33)	
			return;	
	
		fonty=(fontb+alpha);	
	
		xpos=currentx;	
		ypos=currenty;	
	
		for(count2=0;count2<8;count2++)	
		{	
			for(count3=0;count3<8;count3++)	
			{	
				if( (*fonty != 0))	
					PlotPixel(xpos,ypos,INK);	
				else	
					PlotPixel(xpos,ypos,PAPER);	
	
				fonty++;	
				xpos++;	
			}	
			xpos=currentx;	
			ypos++;	
		}	
		currentx+=8;	
	}	
	
}	
/*/////////////////////////////////////////////////////////////	
//	Print Text	
/////////////////////////////////////////////////////////////*/	
void	FontPrint2( int xpos, int ypos, BYTE *text )	
{	
	int	count,count2,count3;	
	int	currentx,currenty;	
	int	alpha;	
	BYTE	*fonty,*fonty2,data,data2;	
	
	count=0;	
	
	xpos*=8;	
	ypos*=8;	
	
	currentx=xpos;	
	currenty=ypos;	
	
	while( *text != 0 )	
	{	
		alpha=(int)*text;	
		text++;	
	
		if(alpha==170) /*="|"	 */
		{	
			currentx=0;	
			currenty+=8;	
			alpha=(int)*text;	
			text++;	
		}	
	
	
		alpha-=32;	
	
		if(alpha>96)	
			alpha-=32;	
	
		alpha*=64;	
		count++;	
		if(count==33)	
			return;	
	
		fonty=(fontb+alpha);	
		fonty2=(GFXfant+alpha);	
	
		xpos=currentx;	
		ypos=currenty;	
	
		for(count2=0;count2<8;count2++)	
		{	
			for(count3=0;count3<8;count3++)	
			{	
				if( (*fonty != 0))	
					PlotPixel(xpos,ypos,0);	
				else	
				{	
					if(*fonty2==1)	
					{	
						data=GetPixel(xpos,ypos);	
						data2=data&15;	
						data2+=3;	
						if(data2>15)	
							data2=15;	
						data&=240;	
						data|=data2;	
						PlotPixel(xpos,ypos,data);	
					}	
				}	
				fonty++;	
				fonty2++;	
				xpos++;	
			}	
			xpos=currentx;	
			ypos++;	
		}	
		currentx+=8;	
	}	
	
}	


/*/////////////////////////////////////////////////////////////	
//	Show a Long Word	
/////////////////////////////////////////////////////////////*/	
void	ShowSix( int xpos, int ypos, unsigned long data )	
{	
	BYTE	convtext[]={"           "};	
	BYTE	printtext[]={"      "};	
	int	count1,count2;	
	BYTE	*pointy;	
	
	/* ADMNOTE: no ultoa in ansi/iso C
	   /*	ultoa( data,&convtext,10);*/
	sprintf(convtext, "%u", data);
	
	count2=0;	
	for(count1=0;count1<7;count1++)	
	{	
		if(convtext[count1]!=32)	
			count2++;	
	}	
	
	pointy=printtext+7;	
	pointy-=count2;	
	
	for(count1=0;count1<count2;count1++)	
	{	
		*pointy=convtext[count1];	
		pointy++;	
	}	
	
	
	for(count1=0;count1<6;count1++)	
	{	
		if(printtext[count1]==32)	
			printtext[count1]=48;	
	}	
	
	FontPrint(xpos,ypos,&printtext);	
}	

/*/////////////////////////////////////////////////////////////	
//	Print Text Small	
/////////////////////////////////////////////////////////////*/	
void	FontPrintSmall( int xpos, int ypos, BYTE *text )	
{	
	int	count,count2,count3;	
	int	currentx,currenty;	
	int	alpha;	
	BYTE	*fonty;	
	
	count=0;	
	
	xpos*=4;	
	ypos*=6;	
	
	currentx=xpos;	
	currenty=ypos;	
	
	while( *text != 0 )	
	{	
		alpha=(int)*text;	
		text++;	
	
		if(alpha==170) /*="ª"	*/
		{	
			currentx=0;	
			currenty+=8;	
			alpha=(int)*text;	
			text++;	
		}	
	
		if(alpha==96) /*="`"	*/
		{	
			INK=(*text)-96;	
			text++;	
			alpha=(int)*text;	
			text++;	
		}	
	
		alpha-=32;	
	
		if(alpha>64)	
			alpha-=32;	
	
		alpha*=64;	
	
		fonty=(fonts+alpha);	
	
		xpos=currentx;	
		ypos=currenty;	
	
		for(count2=0;count2<6;count2++)	
		{	
			for(count3=0;count3<4;count3++)	
			{	
				if( (*fonty != 0))	
					PlotPixel(xpos,ypos,INK);	
				else	
					PlotPixel(xpos,ypos,PAPER);	
	
				fonty++;	
				xpos++;	
			}	
			fonty+=4;	
			xpos=currentx;	
			ypos++;	
		}	
		currentx+=4;	
	}	
	
}	

/*/////////////////////////////////////////////////////////////	
//	Main Code Includes	
/////////////////////////////////////////////////////////////*/	
#include	"mm-core.c"	
#include	"mm-game.c"	
#include	"mm-demo.c"	
/*/////////////////////////////////////////////////////////////	
//	Titles	
/////////////////////////////////////////////////////////////*/	
void	Titles(void)	
{	
	switch(TITLEm)	
	{	
		case	0:	
			TitleSetup();	
			break;	
		case	1:	
			DoPiano();	
			break;	
		case	2:	
			ClearPiano();	
			break;	
		case	3:	
			DoTitleScroll();	
			break;	
		case	4:	
			DoPrefs();	
			break;	
	
	}	
}	
/*/////////////////////////////////////////////////////////////	
//	Do Background Fill 1	
/////////////////////////////////////////////////////////////*/	
void	Fill1(void)	
{	
	int	x,y,block2;	
	BYTE	data;	
	
	block2=0;	
	
	for(y=0;y<24;y++)	
	{	
		for(x=0;x<16;x++)	
		{	
			data=GFXfill[block2];	
			PlotPixel(152+x,40+y,data);	
			block2++;	
		}	
	}	
}	

/*////////////////////////////////////////////////////////////	
//	Do Background Fill 2	
/////////////////////////////////////////////////////////////*/	
void	Fill2(void)	
{	
	int	x,y,block2;	
	BYTE	data;	
	
	block2=384;	
	
	for(y=0;y<24;y++)	
	{	
		for(x=0;x<72;x++)	
		{	
			data=GFXfill[block2];	
			PlotPixel(176+x,40+y,data);	
			block2++;	
		}	
	}	
}	
/*/////////////////////////////////////////////////////////////	
//	Setup Tiltes	
/////////////////////////////////////////////////////////////*/	
void	TitleSetup(void)	
{	
	int	i;	
	
	for(i=0;i<768;i++)	
	{	
		PALover[i]=PALmain[i];	
	}	
	
	PIANOc=0;	
	for(i=0;i<32;i++)	
		PIANOkhit[i]=0;	
	
	TITLEwf=2;	
	TITLEwp=0;	
	SCROLLpos=0;	
	PIXELoff=0;	
	
	UpdateScrollBuffer();	
	FillPixelBuff();	
	
	TITLEm=1;	
	
	modon=MIDASplayModuleSection(mod,0x0d,0x11,0x0d,FALSE);	
	
	TitleSetupExtra();	
	SwapPage();	
	PaletteSet(PALmain);	
	TitleSetupExtra();	
	SwapPage();	
	
}	
/*/////////////////////////////////////////////////////////////	
//	Setup Extra Bits	
/////////////////////////////////////////////////////////////*/	
void	TitleSetupExtra(void)	
{	
	int	i;	
	
	cls(240);	
	
	PAPER=0;	
	for(i=0;i<24;i++)	
	{	
		FontPrint(0,i,"                                ");	
	}	
	DrawFinal();	
	
	SUNm=1;	
	SUNy=32;	
	SUNh=16;	
	DoSun();	
	
	Fill1();	
	Fill2();	
	
	DrawPiano();	
	DrawWilly4(TITLEwf);	
	
	DrawTPlate2();	
	DrawAirBG();	
	
}	
/*/////////////////////////////////////////////////////////////	
//	Draw Piano	
/////////////////////////////////////////////////////////////*/	
void	DrawPiano(void)	
{	
	int	x,y,block2;	
	BYTE	data;	
	
	block2=0;	
	
	for(y=0;y<64;y++)	
	{	
		for(x=0;x<256;x++)	
		{	
			data=GFXpiano[block2];	
			PlotPixel(x,64+y,data);	
			block2++;	
		}	
	}	
}	
/*/////////////////////////////////////////////////////////////	
//	Draw Level Title Plate	
/////////////////////////////////////////////////////////////*/	
void	DrawTPlate2(void)	
{	
	int	x,y;	
	BYTE	data;	
	
	for(y=0;y<8;y++)	
	{	
		for(x=0;x<256;x++)	
		{	
			PlotPixel(x,128+y,GFXtplate[(y*256)+x]);	
		}	
	}	
	FontPrint2(8,16,"1997 Andy Noble");	
}	
/*/////////////////////////////////////////////////////////////	
//	Play The Piano	
/////////////////////////////////////////////////////////////*/	
void	DoPiano(void)	
{	
	int	i,j;	
	BYTE	key1,key2,key3,key4;	
	
	for(i=0;i<32;i++)	
		PIANOkhit[i]=0;	
	
	/* ADM */
/*	MIDASgetPlayStatus(modon,&stat);	
	
	if(stat.pattern==14)	
	{	
		TITLEm=2;	
		MIDASstopModule(modon);	
		modon=0;	
	}	*/

#ifdef USE_MIKMOD
	if(mod->sngpos==17)	
	{	
		TITLEm=2;	
		MIDASstopModule(modon);	
		modon=0;	
	}
#endif

	
	UpdatePianoKeys();	
	SwapPage();	
	
	if(KeyTable[key_f1]==1)	
	{	
		TITLEm=4;	
		PREFSm=0;	
		MIDASstopModule(modon);	
		modon=0;	
	}	
	
	if( KeyTable[key_return]==1 )	
	{	
	
		TITLEm=0;	
		DEMOm=0;	
		MODE=2;	
		MIDASstopModule(modon);	
		modon=0;	
	}	
	
}	
	
/*/////////////////////////////////////////////////////////////	
//	Draw Piano Key	
/////////////////////////////////////////////////////////////*/	
void	DrawKey(int xpos,int ypos,BYTE block)	
{	
	int	x,y,block2;	
	BYTE	data;	
	
	xpos*=8;	
	ypos*=8;	
	
	block2=(WORD)block;	
	block2*=128;	
	
	for(y=0;y<16;y++)	
	{	
		for(x=0;x<8;x++)	
		{	
			data=GFXpkeys[block2];	
			PlotPixel(xpos+x,ypos+y,data);	
			block2++;	
		}	
	}	
	
}	
/*/////////////////////////////////////////////////////////////	
//	Update Piano Keys	
////////////////////////////////////////////////////////////*/	
void	UpdatePianoKeys(void)	
{	
	int	i;	
	
	for(i=0;i<32;i++)	
	{	
		if(PIANOkhit[i]==1)	
		{	
			DrawKey(i,14,PIANOkey[i]+4);	
		}	
		else	
		{	
			DrawKey(i,14,PIANOkey[i]);	
		}	
	}	
}	
/*/////////////////////////////////////////////////////////////	
//	Clear Piano	
/////////////////////////////////////////////////////////////*/	
void	ClearPiano(void)	
{	
	int	i;	
	
	for(i=0;i<32;i++)	
		PIANOkhit[i]=0;	
	
	UpdatePianoKeys();	
	SwapPage();	
	UpdatePianoKeys();	
	SwapPage();	
	
	TITLEm=3;	
	SCROLLpos=0;	
}	
/*/////////////////////////////////////////////////////////////	
//	Draw Willy	
/////////////////////////////////////////////////////////////*/	
void	DrawWilly4(BYTE block)	
{	
	int	x,y,block2;	
	BYTE	data;	
	
	block2=(WORD)block;	
	block2*=256;	
	
	for(y=0;y<16;y++)	
	{	
		for(x=0;x<16;x++)	
		{	
			data=GFXwilly[block2];	
	
			if(data)	
			{	
					PlotPixel(232+x,72+y,data);	
			}	
			else	
			{	
					PlotPixel(232+x,72+y,39);	
			}	
			block2++;	
		}	
	}	
}	

/*/////////////////////////////////////////////////////////////	
//	Do Title Scroll	
/////////////////////////////////////////////////////////////*/	
void	DoTitleScroll(void)	
{	
	SwapPage();	
	
	TITLEwp++;	
	if(TITLEwp==16)	
	{	
		TITLEwp=0;	
		TITLEwf+=2;	
		TITLEwf&=7;	
	}	
	DrawWilly4(TITLEwf+4);	
	
	DrawScroll();	
	
	PIXELoff++;	
	if(PIXELoff==8)	
	{	
		PIXELoff=0;	
		SCROLLpos++;	
		if(SCROLLpos==290)	
		{	
			TITLEm=0;	
			DEMOm=0;	
			MODE=1;	
	
			if(MUSICtype==0)	
				modon=MIDASplayModuleSection(mod,0x00,0x08,0x00,TRUE);	
			else	
				modon=MIDASplayModuleSection(mod,0x09,0x0c,0x00,TRUE);	
	
			if(MUSICon==1)	
				MIDASsetMusicVolume(modon,64);	
			else	
				MIDASsetMusicVolume(modon,0);	
	
		}	
		else	
		{	
			UpdateScrollBuffer();	
			FillPixelBuff();	
		}	
	}	
	
	if(KeyTable[key_f1]==1)	
	{	
		TITLEm=4;	
		PREFSm=0;	
	}	
	
	if( KeyTable[key_return]==1 )	
	{	
	
		TITLEm=0;	
		DEMOm=0;	
		MODE=2;	
	}	
}	

/*/////////////////////////////////////////////////////////////	
//	Update Scroll Buffer	
/////////////////////////////////////////////////////////////*/	
void	UpdateScrollBuffer(void)	
{	
	int	i;	
	
	for(i=0;i<33;i++)	
	{	
		SCROLLbuff[i]=SCROLLtext[SCROLLpos+i];	
	}	
}	

/*/////////////////////////////////////////////////////////////	
//	Fill Pixel buffer with text	
/////////////////////////////////////////////////////////////*/	
void	FillPixelBuff(void)	
{	
	int	i,x,y,dx,dy;	
	WORD	alpha;	
	WORD	point;	
	BYTE	*font,*alias,data,data2;	
	
	dx=0;dy=0;	
	
	for(i=0;i<33;i++)	
	{	
		alpha=(WORD)SCROLLbuff[i];	
		alpha-=32;	
		alpha*=64;	
	
		font=(fontb+alpha);	
		alias=(GFXfant+alpha);	
	
		for(y=0;y<8;y++)	
		{	
			for(x=0;x<8;x++)	
			{	
				data=*font;	
				if(data!=0)	
					PIXELbuff[((dy+y)*264)+(dx+x)]=102;	
				else	
					PIXELbuff[((dy+y)*264)+(dx+x)]=PAPER;	
	
				data2=*alias;	
				if(data2!=0)	
					PIXELbuff[((dy+y)*264)+(dx+x)]=107;	
	
				font++;	
				alias++;	
	
			}	
		}	
		dx+=8;	
	}	
}	

/*/////////////////////////////////////////////////////////////	
//	Draw Scroll	
/////////////////////////////////////////////////////////////*/	
void	DrawScroll(void)	
{	
	int	x,y;	
	
	for(y=0;y<8;y++)	
	{	
		for(x=0;x<256;x++)	
		{	
			PlotPixel(x,152+y,PIXELbuff[(y*264)+(PIXELoff+x)]);	
		}	
	}	
}	

/*/////////////////////////////////////////////////////////////	
//	Do Loading Screen	
/////////////////////////////////////////////////////////////*/	
void	DoLoading(void)	
{	
	switch(LOADm)	
	{	
		case	0:	
			LoadSetup();	
			break;	
		case	1:	
			LoadAnim();	
			break;	
	}	
}	

/*/////////////////////////////////////////////////////////////	
//	Setup Loading Screen	
/////////////////////////////////////////////////////////////*/	
void	LoadSetup(void)	
{	
	int	i,x,y;	
	BYTE	data;	
	
	for(y=0;y<8;y++)	
	{	
		for(x=0;x<32;x++)	
		{	
			data=GFXload[(y*32)+x];	
			PAPER=data;	
			FontPrint(x,9+y," ");	
		}	
	}	
	PAPER=0;	
	INK=124;	
	
	FontPrint(28,23,VERSION);	
	
	SwapPage();	
	
	for(y=0;y<8;y++)	
	{	
		for(x=0;x<32;x++)	
		{	
			data=GFXload[256+((y*32)+x)];	
			PAPER=data;	
			FontPrint(x,9+y," ");	
		}	
	}	
	PAPER=0;	
	INK=124;	
	FontPrint(28,23,VERSION);	
	
	LOADm=1;	
}	

/*/////////////////////////////////////////////////////////////	
//	Animate Loading Screen	
/////////////////////////////////////////////////////////////*/	
void	LoadAnim(void)	
{	
	LOADp++;	
	if(LOADp==25)	
	{	
		LOADp=0;	
		SwapPage();	
	}	
	else	
	{	
		WaitVR();	
	}	
	
	if(AnyKeyx()==1)	
	{	
		MODE=0;	
	}	
}	

/*/////////////////////////////////////////////////////////////	
//	Preferences Screen	
/////////////////////////////////////////////////////////////*/	
void	DoPrefs(void)	
{	
	switch(PREFSm)	
	{	
		case	0:	
			SetupPrefs();	
			break;	
		case	1:	
			PrefsUpdate();	
			break;	
	}	
}	

/*/////////////////////////////////////////////////////////////	
//	Setup Prefs	
/////////////////////////////////////////////////////////////*/	
void	SetupPrefs(void)	
{	
	int	count;	
	
	for(count=0;count<768;count++)	
	{	
		PALover[count]=PALmain[count];	
	}	
	
	DoPrefsExtra();	
	SwapPage();	
	
	PaletteSet(PALover);	
	DoPrefsExtra();	
	
	PREFSh1=0;	
	PREFSh2=0;	
	PREFSh3=0;	
	PREFSm=1;	
	
	modon=MIDASplayModuleSection(mod,0x12,0x15,0x12,TRUE);	
}	

/*/////////////////////////////////////////////////////////////	
//	Prefs Screen Setup	
/////////////////////////////////////////////////////////////*/	
void	DoPrefsExtra(void)	
{	
	int	x,y,i;	
	
	i=240;	
	
	cls(0);	
	
	for(x=0;x<32;x++)	
	{	
		PAPER=i;			
		FontPrint(x,0," ");	
		i++;	
		if(i>254)	
			i=240;	
	}	
	
	for(y=1;y<24;y++)	
	{	
		PAPER=i;			
		FontPrint(31,y," ");	
		i++;	
		if(i>254)	
			i=240;	
	}	
	
	for(x=1;x<32;x++)	
	{	
		PAPER=i;			
		FontPrint(31-x,23," ");	
		i++;	
		if(i>254)	
			i=240;	
	}	
	
	for(y=1;y<23;y++)	
	{	
		PAPER=i;			
		FontPrint(0,23-y," ");	
		i++;	
		if(i>254)	
			i=240;	
	}	
	
	
	PAPER=0;	
	INK=7;	
	
	FontPrintSmall(24,2, "`fManic Miner PC");	
	
	FontPrintSmall(23,3, "`g(C)`e1997 `gAndy Noble");	
	FontPrintSmall(10,4, "`gBased on an original game by `eMatthew Smith`g.");	
	FontPrintSmall(7,5,  "`g(C) `e1983 `fBUG-BYTE`g Ltd. And `fSoftware Projects`g Ltd.");	
	FontPrintSmall(18,6,  "`g(C) `e1997 `fAlchemist Research`g.");	
	
	FontPrintSmall(7,8, "`dProgramming`g, `dGraphics`e...............`fAndy Noble`e.");	
	FontPrintSmall(7,9, "`dMUSIC ARRANGED BY`e.................`fMatt Simmonds`e.");	
	FontPrintSmall(7,10,"`dExtra Levels`e.................`fLee `d'`gBLOOD!`d' `fTonks`e.");	
	FontPrintSmall(7,11,"`dTesting and Ideas`e.................`fEwan Christie`e.");	
	
	FontPrintSmall(3,13, "`dI would just like to say ThankYou to the following people");	
	
	FontPrintSmall(3,15, "`fSahara Surfers`d..............`eFor MIDAS Digital Audio System");	
	FontPrintSmall(3,16, "`fCharles Scheffold and Thomas Pytel`d.....`eFor the fab PMODE/W");	
	FontPrintSmall(3,17, "`fTyrone L. Cartwright`d............`eFor help with the bad guys");	
	
	FontPrintSmall(3,19, "`fDavid H. Tolley`d.................`eFor the constant slaggings");	
	FontPrintSmall(3,20, "`fGerton Lunter`d........................`eFor the excellent Z80");	
	FontPrintSmall(3,21, "`fJames McKay`d.................`eFor the equally excellent X128");	
	
	FontPrintSmall(5,22, "`cEverybody who e-mailed me with words of encouragement.");	
	
	FontPrintSmall(13,24,"`fAnd all the Guys on COMP.SYS.SINCLAIR");	
	FontPrintSmall(4,25, "`eWho keep me informed and amused about all things Speccy.");	
	
	FontPrintSmall(15,27,"`f    F2          F3          F4");	
	FontPrintSmall(15,28,"`bGame Speed    Levels      Melody");	
	FontPrintSmall(15,29,"`c ORIGINAL    ORIGINAL    ORIGINAL");	
	
	PrintSpeed();	
	PrintMaps();	
	PrintMusic();	
	
}	

/*/////////////////////////////////////////////////////////////	
//	Update setup screen	
/////////////////////////////////////////////////////////////*/	
void	PrefsUpdate(void)	
{	
	RotPal();	
	SwapPage();	
	PaletteSet(PALover);	
	
	if(PREFSh1==1)	
	{	
		if(KeyTable[key_f2]!=1)	
		{	
			SPEED++;	
			if(SPEED==5)	
				SPEED=0;	
		}	
		PREFSh1=0;	
	}	
	
	if(KeyTable[key_f2]==1)	
	{	
		PREFSh1=1;	
	}	
	
	if(PREFSh2==1)	
	{	
		if(KeyTable[key_f3]!=1)	
		{	
			TONKS++;	
			TONKS&=1;	
		}	
		PREFSh2=0;	
	}	
	
	if(KeyTable[key_f3]==1)	
	{	
		PREFSh2=1;	
	}	
	
	if(PREFSh3==1)	
	{	
		if(KeyTable[key_f4]!=1)	
		{	
			MUSICtype++;	
			MUSICtype&=1;	
		}	
		PREFSh3=0;	
	}	
	
	if(KeyTable[key_f4]==1)	
	{	
		PREFSh3=1;	
	}	
	
	if(KeyTable[key_esc]==1)	
	{	
		MODE=0;	
		TITLEm=0;	
		MIDASstopModule(modon);	
		modon=0;	
	}	
	
	PrintSpeed();	
	PrintMaps();	
	PrintMusic();	
	
}	
	
/*/////////////////////////////////////////////////////////////	
//	Display Game Speed	
/////////////////////////////////////////////////////////////*/	
void	PrintSpeed(void)	
{	
	switch(SPEED)	
	{	
		case	0:	
			INK=1;	
			FontPrintSmall(16,29," SILLY! ");	
			break;	
		case	1:	
			INK=3;	
			FontPrintSmall(16,29,"  HARD  ");	
			break;	
		case	2:	
			INK=4;	
			FontPrintSmall(16,29,"  1997  ");	
			break;	
		case	3:	
			INK=7;	
			FontPrintSmall(16,29,"ORIGINAL");	
			break;	
		case	4:	
			INK=5;	
			FontPrintSmall(16,29," BORING ");	
			break;	
	}	
}	

/*/////////////////////////////////////////////////////////////	
//	Display Game Maps	
/////////////////////////////////////////////////////////////*/	
void	PrintMaps(void)	
{	
	switch(TONKS)	
	{	
		case	0:	
			INK=7;	
			FontPrintSmall(28,29,"ORIGINAL");	
			break;	
		case	1:	
			INK=4;	
			FontPrintSmall(28,29," BLOOD! ");	
			break;	
	}	
}	

/*/////////////////////////////////////////////////////////////	
//	Display Game Music	
/////////////////////////////////////////////////////////////*/	
void	PrintMusic(void)	
{	
	switch(MUSICtype)	
	{	
		case	0:	
			INK=4;	
			FontPrintSmall(40,29,"  1997  ");	
			break;	
		case	1:	
			INK=7;	
			FontPrintSmall(40,29,"ORIGINAL");	
			break;	
	}	
}	

/*/////////////////////////////////////////////////////////////	
//	Save Stuff on Exit	
/////////////////////////////////////////////////////////////*/	
void	SaveInfo(void)	
{	
	FILE	*file;	
	
	file=fopen( "mm-conf.cfg", "wb" );	
	fwrite( &HISCORE, sizeof(unsigned long), 1, file );	
	fwrite( &SPEED, sizeof(BYTE), 1, file );	
	fwrite( &TONKS, sizeof(BYTE), 1, file );	
	fwrite( &MUSICtype, sizeof(BYTE), 1, file );	
	fclose( file );	
}	

/*/////////////////////////////////////////////////////////////	
//	Load Stuff at Start	
/////////////////////////////////////////////////////////////*/	
void	LoadInfo(void)	
{	
	FILE	*file;	
	
	file=fopen( "mm-conf.cfg", "rb" );	
	if(file==NULL)	
	{	
		HISCORE=0;	
		SPEED=3;	
		TONKS=0;	
		MUSICtype=0;	
		return;	
	}	
	fread( &HISCORE, sizeof(unsigned long), 1, file );	
	fread( &SPEED, sizeof(BYTE), 1, file );	
	fread( &TONKS, sizeof(BYTE), 1, file );	
	fread( &MUSICtype, sizeof(BYTE), 1, file );	
	fclose( file );	
}	
	
/*/////////////////////////////////////////////////////////////	
//	Pre Vertical Retrace	
/////////////////////////////////////////////////////////////*/	
void	PREvr(void)	
{	
	FrCt++;	
}	

/*/////////////////////////////////////////////////////////////	
//	Setup Sound Card	
/////////////////////////////////////////////////////////////*/	
void	SetupSound(void)	
{	
	FILE	*file;	
	
	file=fopen( "mm-midas.cfg", "rb" );	
	if((file==NULL)||(FORCE==1))	
	{	
		if(file!=NULL)	
			fclose(file);	
	
		MIDASconfig();	
		MIDASsaveConfig("mm-midas.cfg");	
	}	
	else	
	{	
		fclose(file);	
		MIDASloadConfig("mm-midas.cfg");	
	}	
}	
