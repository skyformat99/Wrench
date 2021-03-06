/*
droid vnc server - Android VNC server
Copyright (C) 2009 Jose Pereira <onaips@gmail.com>

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 3 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "common.h"

#include "rfb/rfb.h"
#include "libvncserver/scale.h"
#include "rfb/keysym.h"
#include "screenrecord.h"

#define CONCAT2(a,b) a##b
#define CONCAT2E(a,b) CONCAT2(a,b)
#define CONCAT3(a,b,c) a##b##c
#define CONCAT3E(a,b,c) CONCAT3(a,b,c)

char VNC_PASSWORD[256] = "";
/* Android already has 5900 bound natively in some devices. */
int VNC_PORT=5901;

unsigned int *cmpbuf;
unsigned int *vncbuf;

static rfbScreenInfoPtr vncscr;

uint32_t idle = 0;
uint32_t standby = 1;
uint16_t rotation = 0;
uint16_t scaling = 100;
uint8_t display_rotate_180 = 0;

void (*update_screen)(void)=NULL;

#define PIXEL_TO_VIRTUALPIXEL_FB(i,j) ((j+scrinfo.yoffset)*scrinfo.xres_virtual+i+scrinfo.xoffset)
#define PIXEL_TO_VIRTUALPIXEL(i,j) ((j*(screenformat.width + screenformat.pad))+i)

#define OUT 8 
#include "updateScreen.c" 
#undef OUT

#define OUT 16
#include "updateScreen.c"
#undef OUT

#define OUT 32 
#include "updateScreen.c"
#undef OUT

inline int getCurrentRotation()
{
  return rotation;
}

rfbNewClientHookPtr clientHook(rfbClientPtr cl)
{
  if (scaling!=100)
  {
    rfbScalingSetup(cl, vncscr->width*scaling/100.0, vncscr->height*scaling/100.0);
    L("Scaling to w=%d  h=%d\n",(int)(vncscr->width*scaling/100.0), (int)(vncscr->height*scaling/100.0));
    //rfbSendNewScaleSize(cl);
  }

  return RFB_CLIENT_ACCEPT;
}


void initVncServer(int argc, char **argv)
{ 

  vncbuf = calloc(screenformat.width * screenformat.height, screenformat.bitsPerPixel/CHAR_BIT);
  cmpbuf = calloc(screenformat.width * screenformat.height, screenformat.bitsPerPixel/CHAR_BIT);

  assert(vncbuf != NULL);
  assert(cmpbuf != NULL);

  if (rotation==0 || rotation==180) 
    vncscr = rfbGetScreen(&argc, argv, screenformat.width , screenformat.height, 0 /* not used */ , 3,  screenformat.bitsPerPixel/CHAR_BIT);
  else
    vncscr = rfbGetScreen(&argc, argv, screenformat.height, screenformat.width, 0 /* not used */ , 3,  screenformat.bitsPerPixel/CHAR_BIT);

  assert(vncscr != NULL);

  vncscr->desktopName = "Android";
  vncscr->frameBuffer =(char *)vncbuf;
  vncscr->port = VNC_PORT;
  //vncscr->kbdAddEvent = keyEvent;
  //vncscr->ptrAddEvent = ptrEvent;
  vncscr->newClientHook = (rfbNewClientHookPtr)clientHook;
  //vncscr->setXCutText = CutText;

  if (strcmp(VNC_PASSWORD,"")!=0)
  {
    char **passwords = (char **)malloc(2 * sizeof(char **));
    passwords[0] = VNC_PASSWORD;
    passwords[1] = NULL;
    vncscr->authPasswdData = passwords;
    vncscr->passwordCheck = rfbCheckPasswordByList;
  } 

  vncscr->httpDir = "webclients/";
  //  vncscr->httpEnableProxyConnect = TRUE;
  vncscr->sslcertfile = "self.pem";

  vncscr->serverFormat.redShift = screenformat.redShift;
  vncscr->serverFormat.greenShift = screenformat.greenShift;
  vncscr->serverFormat.blueShift = screenformat.blueShift;

  vncscr->serverFormat.redMax = (( 1 << screenformat.redMax) -1);
  vncscr->serverFormat.greenMax = (( 1 << screenformat.greenMax) -1);
  vncscr->serverFormat.blueMax = (( 1 << screenformat.blueMax) -1);

  vncscr->serverFormat.trueColour = TRUE; 
  vncscr->serverFormat.bitsPerPixel = screenformat.bitsPerPixel;

  vncscr->alwaysShared = TRUE;
  vncscr->handleEventsEagerly = TRUE;
  vncscr->deferUpdateTime = 5;

  rfbInitServer(vncscr);

  //assign update_screen depending on bpp
  if (vncscr->serverFormat.bitsPerPixel == 32)
    update_screen=&CONCAT2E(update_screen_,32);
  else if (vncscr->serverFormat.bitsPerPixel == 16)
    update_screen=&CONCAT2E(update_screen_,16);
  else if (vncscr->serverFormat.bitsPerPixel == 8)
    update_screen=&CONCAT2E(update_screen_,8);
  else {
    L("Unsupported pixel depth: %d\n",
      vncscr->serverFormat.bitsPerPixel);

    close_app();
    exit(-1);
  }

  /* Mark as dirty since we haven't sent any updates at all yet. */
  rfbMarkRectAsModified(vncscr, 0, 0, vncscr->width, vncscr->height);
}



void rotate(int value)
{

  L("rotate()\n");

  if (value == -1 || 
      ((value == 90 || value == 270) && (rotation == 0 || rotation == 180)) ||
      ((value == 0 || value == 180) && (rotation == 90 || rotation == 270))) {
        int h = vncscr->height;
        int w = vncscr->width;

        vncscr->width = h;
        vncscr->paddedWidthInBytes = h * screenformat.bitsPerPixel / CHAR_BIT;
        vncscr->height = w;

        rfbClientIteratorPtr iterator;
        rfbClientPtr cl;
        iterator = rfbGetClientIterator(vncscr);
        while ((cl = rfbClientIteratorNext(iterator)) != NULL)
        cl->newFBSizePending = 1;
      }

  if (value == -1) {
    rotation += 90;
    rotation %= 360;
  } else {
    rotation = value;  
  }
 
  rfbMarkRectAsModified(vncscr, 0, 0, vncscr->width, vncscr->height);
}


void close_app()
{ 	
  closeScreenRecord();
  exit(0); /* normal exit status */
}


void printUsage(char **argv)
{
  L("\nandroidvncserver [parameters]\n"
    "-h\t\t- Print this help\n"
    "-p <password>\t- Password to access server\n"
    "-r <rotation>\t- Screen rotation (degrees) (0,90,180,270)\n"
    "-R <host:port>\t- Host for reverse connection\n" 
    "-s <scale>\t- Scale percentage (20,30,50,100,150)\n"
    "-z\t- Rotate display 180º (for zte compatibility)\n\n");
}


#include <time.h>

int main(int argc, char **argv)
{
  //pipe signals
  signal(SIGINT, close_app);
  signal(SIGKILL, close_app);
  signal(SIGILL, close_app);
  long usec;

  if(argc > 1) {
    int i=1;
    int r;
    while(i < argc) {
      if(*argv[i] == '-') {
        switch(*(argv[i] + 1)) {
        case 'h':
          printUsage(argv);
          exit(0); 
          break;
        case 'p': 
          i++; 
          strcpy(VNC_PASSWORD,argv[i]);
          break;
        case 'z': 
          i++; 
          display_rotate_180=1;
          break;
        case 'P': 
          i++; 
          VNC_PORT=atoi(argv[i]);
          break;
        case 'r':
          i++; 
          r = atoi(argv[i]);
          if (r==0 || r==90 || r==180 || r==270)
            rotation = r;
          L("rotating to %d degrees\n",rotation);
          break;
        case 's':
          i++;
          r=atoi(argv[i]); 
          if (r >= 1 && r <= 150)
            scaling = r;
          else 
            scaling = 100;
          L("scaling to %d%%\n",scaling);
          break;
        }
      }
      i++;
    }
  }

    L("Initializing grabber method...\n");
    initScreenRecord();

    L("Initializing VNC server:\n");
    L("	width:  %d\n", (int)screenformat.width);
    L("	height: %d\n", (int)screenformat.height);
    L("	bpp:    %d\n", (int)screenformat.bitsPerPixel);
    L("	port:   %d\n", (int)VNC_PORT);


    L("Colourmap_rgba=%d:%d:%d:%d    lenght=%d:%d:%d:%d\n", screenformat.redShift, screenformat.greenShift, screenformat.blueShift,screenformat.alphaShift,
      screenformat.redMax,screenformat.greenMax,screenformat.blueMax,screenformat.alphaMax);  

    initVncServer(argc, argv);

    while (!gStopRequested) {
        usec=(vncscr->deferUpdateTime+standby)*1000;
        //clock_t start = clock();
        rfbProcessEvents(vncscr,usec);

      if (vncscr->clientHead == NULL)
      {
        idle=1;
        standby = 1000;
        continue;
      } else {
        idle = 0;
        standby = 15;
      }

      update_screen(); 
      //printf ( "%f\n", ( (double)clock() - start )*1000 / CLOCKS_PER_SEC );
    }
    close_app();
    return 0;
}
