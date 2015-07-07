/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * gmain.c: gtk main routines for pho, an image viewer.
 *
 * Copyright 2004 by Akkana Peck.
 * You are free to use or modify this code under the Gnu Public License.
 */

#include "pho.h"
#include "dialogs.h"
#include "exif/phoexif.h"

#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>

#include <stdlib.h>       /* for getenv() */
#include <unistd.h>
#include <string.h>
#include <ctype.h>

char * gCapFileFormat = "Captions";

/* Toggle a variable between two modes, preferring the first.
 * If it's anything but mode1 it will end up as mode1.
 */
#define ToggleBetween(val,mode1,mode2) (val != mode1 ? mode1 : mode2)

/* If we're switching into scaling mode because the user pressed - or +/=,
 * which scaling mode should we switch into?
 */
static int ModeForScaling(int oldmode)
{
    switch (oldmode)
    {
      case PHO_SCALE_IMG_RATIO:
      case PHO_SCALE_FULLSIZE:
      case PHO_SCALE_NORMAL:
        return PHO_SCALE_IMG_RATIO;

      case PHO_SCALE_FIXED:
          return PHO_SCALE_FIXED;

      case PHO_SCALE_FULLSCREEN:
      default:
        return PHO_SCALE_SCREEN_RATIO;
    }
}

static void RunPhoCommand()
{
    char* pos;
    char* cmd = getenv("PHO_CMD");
    if (cmd == 0) cmd = "gimp";
    else if (gDebug)
        printf("Calling PHO_CMD %s\n", cmd);

    /* PHO_CMD command can have a %s in it but doesn't need to.
     * If it does, we'll use sprintf and system(),
     * otherwise we'll properly use vfork() and execlp().
     */
    if ((pos = strchr(cmd, '%')) == 0 || pos[1] != 's') {
        if (fork() == 0) {      /* child process */
            if (gDebug)
                printf("Child: about to exec %s, %s\n",
                       cmd, gCurImage->filename);
            execlp(cmd, cmd, gCurImage->filename, (char*)0);
        }
    }
    else {
        /* If there's a %s in the filename, then we'll pass
         * the command to sh -c. That means we should put
         * single quotes around the filename to guard against
         * evil filenames like "`rm -rf ~`".
         * But that also means we have to escape any single
         * quotes in that filename.
         */
        int len;
        char *buf, *bufp, *cmdp;
        int numquotes = 0;
        int did_subst = 0;

        /* Count any single quotes in the filename */
        for (len = 0, buf = gCurImage->filename; buf[len]; ++len)
            if (buf[len] == '\'')
                ++numquotes;
        /* len is now the # chars in filename, not including null */

        buf = malloc(strlen(cmd) + len + numquotes + 1);
        /* -2 because we lose the %s, +2 for the two quotes we add,
         * and another for each \ we need to add to escape a quote.
         */

        /* copy cmd into buf, substituting the filename for %s */
        for (bufp = buf, cmdp = cmd; *cmdp; )
        {
            if (!did_subst && cmdp[0] == '%' && cmdp[1] == 's') {
                *(bufp++) = '\'';
                strncpy(bufp, gCurImage->filename, len);
                bufp += len;
                *(bufp++) = '\'';
                cmdp += 2;
                did_subst = 1;
            }
            else if (cmd[0] == '\'') {
                *(bufp++) = '\\';
                *(bufp++) = '\'';
            }
            else
                *(bufp++) = *(cmdp++);
        }
        *bufp = 0;
        if (fork() == 0) {      /* child process */
            if (gDebug)
                printf("Child: about to exec sh -c \"%s\"\n", buf);
            execl("/bin/sh", "sh", "-c", buf, (char*)0);
        }
    }
}

void TryScale(float times)
{
    /* Save the view modes, in case this fails */
    int saveScaleMode = gScaleMode;
    double saveScaleRatio = gScaleRatio;
    int saveDisplayMode = gDisplayMode;

    if (SetViewModes(gDisplayMode, ModeForScaling(gScaleMode),
                     gScaleRatio * times) == 0)
        return;

    /* Oops! It didn't work. Try to reset back to previous settings */
    SetViewModes(saveDisplayMode, saveScaleMode, saveScaleRatio);
}

//mod by chris
gint HandleGlobalKeys(GtkWidget* widget, GdkEventKey* event)
{
    if (gDebug) printf("\nKey event\n");
    switch (event->keyval)
    {
      case GDK_d:
          DeleteImage(gCurImage);
          break;
      case GDK_n:
      case GDK_space:
          /* If we're in slideshow mode, cancel the slideshow */
          if (gDelaySeconds > 0) {
              gDelaySeconds = 0;
          }
          else if (NextImage() != 0) {
              if (Prompt("quit pho?", "quit", "continue", "qx \n", "cn") != 0)
                  EndSession();
          }
          return TRUE;
      case GDK_p:
      case GDK_BackSpace:
          PrevImage();
          return TRUE;
      case GDK_Home:
          gCurImage = 0;
          NextImage();
          return TRUE;
      case GDK_End:
          gCurImage = gFirstImage->prev;
          ThisImage();
          return TRUE;
      case GDK_equal:   /* Get out of any weird display modes */
          SetViewModes(PHO_DISPLAY_NORMAL, PHO_SCALE_NORMAL, 1.);
          ShowImage();
          return TRUE;
      #if 0
      case GDK_f:   /* Full size mode: show image bit-for-bit */
          /* Don't respond to ctrl-F -- that might be an attempt
           * to edit in a text field in the keywords dialog
           */
          if (event->state & GDK_CONTROL_MASK)
              return FALSE;
          SetViewModes(gDisplayMode,
                       ToggleBetween(gScaleMode,
                                     PHO_SCALE_FULLSIZE, PHO_SCALE_NORMAL),
                       1.);
          return TRUE;
      #endif
      case GDK_F:   /* Full screen mode: as big as possible on screen */
          SetViewModes(gDisplayMode,
                       ToggleBetween(gScaleMode,
                       PHO_SCALE_FULLSCREEN, PHO_SCALE_NORMAL),
                       1.);
          return TRUE;
      case GDK_f:
          SetViewModes((gDisplayMode == PHO_DISPLAY_PRESENTATION)
                       ? PHO_DISPLAY_NORMAL
                       : PHO_DISPLAY_PRESENTATION,
                       gScaleMode, gScaleRatio);
          return TRUE;
      case GDK_0:
      case GDK_1:
      case GDK_2:
      case GDK_3:
      case GDK_4:
      case GDK_5:
      case GDK_6:
      case GDK_7:
      case GDK_8:
      case GDK_9:
          if (event->state & GDK_MOD1_MASK) /* alt-num: add 10 to num */
              ToggleNoteFlag(gCurImage, event->keyval - GDK_0 + 10);
          else
              ToggleNoteFlag(gCurImage, event->keyval - GDK_0);
          return TRUE;
      case GDK_r:   /* make life easier for xv switchers */
      //case GDK_t:
      case GDK_Right:
      case GDK_KP_Right:
          ScaleAndRotate(gCurImage, 90);
          return TRUE;
      case GDK_R:   /* make life easier for xv users */
      //case GDK_T:
      //case GDK_l:
      //case GDK_L:
      case GDK_Left:
      case GDK_KP_Left:
          ScaleAndRotate(gCurImage, 270);
          return TRUE;
      case GDK_Up:
      case GDK_Down:
          ScaleAndRotate(gCurImage, 180);
          return TRUE;
      case GDK_i:
      case GDK_plus:
      case GDK_KP_Add:
          if (event->state & GDK_CONTROL_MASK)
              TryScale(1.25);
          else
              TryScale(2.);
          return TRUE;
      case GDK_o:
      case GDK_minus:
      case GDK_KP_Subtract:
          if (event->state & GDK_CONTROL_MASK)
              TryScale(.8);
          else
              TryScale(.5);
          return TRUE;
      #if 0
      case GDK_x:  /* start gimp, or some other app */
          RunPhoCommand();
          break;
      case GDK_g:
          ToggleInfo();
          return TRUE;
      case GDK_K:
          ToggleKeywordsMode();
          return TRUE;
      case GDK_O:
          ChangeWorkingFileSet();
          return TRUE;
      #endif
      //case GDK_Escape:
      case GDK_q:
          EndSession();
          return TRUE;
      default:
          if (gDebug)
              printf("Don't know key 0x%lu\n", (unsigned long)(event->keyval));
          return FALSE;
    }
    /* Keep gcc 2.95 happy: */
    return FALSE;
}

PhoImage* AddImage(char* filename)
{
    PhoImage* img = NewPhoImage(filename);
    if (gDebug)
        printf("Adding image %s\n", filename);
    if (!img) {
        fprintf(stderr, "Out of memory!\n");
        exit(1);
    }
    /* Make img the new last image in the list */
    AppendItem(img);
    return img;
}

static void parseGeom(char* geom, int* width, int* height)
{
    int num;
    if (!isdigit(*geom)) {
        printf("Geometry '%s' must start with a digit\n", geom);
        Usage();
    }
    num = atoi(geom);
    if (num > 0)
        *width = num;

    /* Now see if there's a height */
    while (*(++geom)) {
        if (*geom == 'x') {
            ++geom;
            if (!isdigit(*geom)) {
                printf("Number after 'x' in geometry must start with a digit, not %s\n", geom);
                Usage();
            }
            num = atoi(geom);
            if (num > 0)
                *height = num;
            return;
        }
    }
}

/* CheckArg takes a string, like -Pvg, and sets all the relevant flags. */
static void CheckArg(char* arg)
{
    for ( ; *arg != 0; ++arg)
    {
        if (*arg == '-')
            ;
        else if (*arg == 'd')
            gDebug = 1;
        else if (*arg == 'h')
            Usage();
        else if (*arg == 'v')
            VerboseHelp();
        else if (*arg == 'n')
            gMakeNewWindows = 1;
        else if (*arg == 'p') {
            gDisplayMode = PHO_DISPLAY_PRESENTATION;
            if (arg[1] == '\0') {
                /* Normal presentation mode -- center everything
                 * and size to the current display.
                 */
                gPresentationWidth = 0;
                gPresentationHeight = 0;
            } else {
                char* geom = arg+1;
                if (*geom == 'p') ++geom;
                gPresentationWidth = 0;
                gPresentationHeight = 0;
                parseGeom(geom, &gPresentationWidth, &gPresentationHeight);
            }
        } else if (*arg == 'P')
            gDisplayMode = PHO_DISPLAY_NORMAL;
        else if (*arg == 'k') {
            gDisplayMode = PHO_DISPLAY_KEYWORDS;
            gScaleMode = PHO_SCALE_FIXED;
            gScaleRatio = 0.0;
        } else if (*arg == 's') {
            /* find the slideshow delay time, from e.g. pho -s2 */
            if (isdigit(arg[1]))
                gDelaySeconds = atoi(arg+1);
            else Usage();
            if (gDebug)
                printf("Slideshow delay %d seconds\n", gDelaySeconds);
        } else if (*arg == 'r') {
            gRepeat = 1;
        } else if (*arg == 'c') {
            gCapFileFormat = strdup(arg+1);
            if (gDebug)
                printf("Format set to '%s'\n", gCapFileFormat);

            /* Can't follow this with any other letters -- they're all
             * part of the filename -- so return.
             */
            return;
	    }
    }
}

int main(int argc, char** argv)
{
    /* Initialize some defaults from environment variables,
     * before reading cmdline args.
     */
    int options = 1;

    char* env = getenv("PHO_ARGS");
    if (env && *env)
        CheckArg(env);

    while (argc > 1)
    {
        if (argv[1][0] == '-' && options) {
            if (strcmp(argv[1], "--"))
                CheckArg(argv[1]);
            else
                options = 0;
        }
        else {
            AddImage(argv[1]);
        }
        --argc;
        ++argv;
    }

    if (gFirstImage == 0)
        Usage();

    /* Initialize some variables associated with the notes flags */
    InitNotes();

    /* See http://www.gtk.org/tutorial */
    gtk_init(&argc, &argv);

    /* Must init rgb system explicitly, else we'll crash
     * in the first gdk_pixbuf_render_to_drawable(),
     * calling gdk_draw_rgb_image_dithalign():
     * (Is this still true in gtk2?)
     */
    gdk_rgb_init();

    gPhysMonitorWidth = gMonitorWidth = gdk_screen_width();
    gPhysMonitorHeight = gMonitorHeight = gdk_screen_height();

    /* Load the first image */
    if (NextImage() != 0)
        exit(1);

    gtk_main();
    return 0;
}

