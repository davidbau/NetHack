/* $NHDT-Header: NetHack 5.0 termcap.c 2026/06/20_16:31:40 NetHack-5.0:1.84 $ */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/*-Copyright (c) Pasi Kallinen, 2018. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"

#if defined(TTY_GRAPHICS) && !defined(NO_TERMS)

/* leave this undefined; it produces bad screen output with rxvt-unicode */
/*#define DECgraphicsOptimization*/

#include "wintty.h"
#include "tcap.h"

#define Tgetstr(key) (tgetstr(key, &tbufptr))

static char *s_atr2str(int);
static char *e_atr2str(int);

void cmov(int, int);
void nocmov(int, int);
void term_start_extracolor(uint32, uint16);
void term_end_extracolor(void);

#if defined(TERMLIB)
#if (!defined(UNIX) || !defined(TERMINFO)) && !defined(TOS)
static void analyze_seq(char *, int *, int *);
#endif
#if defined(UNIX) && defined(TERMINFO)
static int indexed_color_count(void);
#endif
#endif
#if (defined(TERMLIB) || defined(ANSI_DEFAULT))
static void init_hilite(void);
static void kill_hilite(void);
#endif

/* (see tcap.h) -- nh_CM, nh_ND, nh_CD, nh_HI,nh_HE, nh_US,nh_UE, ul_hack */
struct tc_lcl_data tc_lcl_data = { 0, 0, 0, 0, 0, 0, 0, FALSE };

static char *nh_VI = (char *) 0; /* cursor_invisible */
static char *nh_VE = (char *) 0; /* cursor_normal */
/*static char *nh_VS = (char *) 0;*/ /* cursor_visible (highlighted cursor) */
#if defined(TERMLIB) || defined(CHANGE_COLOR)
static char *nh_Ic = (char *) 0; /* initialize_color */
#endif

#if !defined(__NetBSD__)
static char *UP, *BC;
#ifdef TERMLIB
static char PC = '\0';
#endif
#endif

static char *HO, *CL, *CE, *XD, *SO, *SE, *TI, *TE;
static char *VS, *VE;
static char *ME, *MR, *MB, *MH, *MD;
static char *ZH, *ZR;

#ifdef TERMLIB
boolean dynamic_HIHE = FALSE;
static int SG;
static char tbuf[512];
#endif /*TERMLIB*/

#ifdef TOS
const char *hilites[CLR_MAX]; /* terminal escapes for the various colors */
#else
char NEARDATA *hilites[CLR_MAX]; /* terminal escapes for the various colors */
#endif

static char *KS = (char *) 0, *KE = (char *) 0; /* keypad sequences */
static char nullstr[] = "";

#if defined(ASCIIGRAPH) && !defined(NO_TERMS)
extern boolean HE_resets_AS;
#endif

#ifndef TERMLIB
static char tgotobuf[20];
#ifdef TOS
#define tgoto(fmt, x, y) (Sprintf(tgotobuf, fmt, y + ' ', x + ' '), tgotobuf)
#else
#define tgoto(fmt, x, y) (Sprintf(tgotobuf, fmt, y + 1, x + 1), tgotobuf)
#endif
#endif /* TERMLIB */

/* these don't need to be part of 'struct instance_globals g' */
static char tty_standout_on[16], tty_standout_off[16];

void
term_startup(int *wid, int *hgt)
{
#ifdef TERMLIB
    const char *term;
    char *tptr;
    char *tbufptr, *pc;
    int i;

#ifdef VMS
    term = verify_termcap();
    if (!term)
#endif
        term = getenv("TERM");

#if defined(TOS) && defined(__GNUC__)
    if (!term)
        term = "builtin"; /* library has a default */
#endif
    if (!term)
#endif /* TERMLIB */
#ifndef ANSI_DEFAULT
        error("Can't get TERM.");
#else
#ifdef TOS
    {
        CO = 80;
        LI = 25;
        TI = VS = VE = TE = nullstr;
        /*
         * FIXME:  These variables ought to be declared 'const' (instead
         * of using nhStr() to cast away const) to avoid '-Wwrite-sttings'
         * warnings about assigning string literals to them.
         */
        HO = nhStr("\033H");
        CE = nhStr("\033K"); /* the VT52 termcap */
        UP = nhStr("\033A");
        nh_CM = nhStr("\033Y%c%c"); /* used with function tgoto() */
        nh_ND = nhStr("\033C");
        XD = nhStr("\033B");
        BC = nhStr("\033D");
        SO = nhStr("\033p");
        SE = nhStr("\033q");
        /* HI and HE will be updated in init_hilite if we're using color */
        nh_HI = nhStr("\033p");
        nh_HE = nhStr("\033q");
        *wid = CO;
        *hgt = LI;
        CL = nhStr("\033E"); /* last thing set */
        return;
    }
#else /* TOS */
    {
#ifdef MICRO
        get_scr_size();
#ifdef CLIPPING
        if (CO < COLNO || LI < ROWNO + 3)
            setclipped();
#endif
#endif
        HO = nhStr("\033[H");
        /*              nh_CD = nhStr("\033[J"); */
        CE = nhStr("\033[K"); /* the ANSI termcap */
#ifndef TERMLIB
        nh_CM = nhStr("\033[%d;%dH");
#else
        nh_CM = nhStr("\033[%i%d;%dH");
#endif
        UP = nhStr("\033[A");
        nh_ND = nhStr("\033[C");
        XD = nhStr("\033[B");
#ifdef MICRO /* backspaces are non-destructive */
        BC = nhStr("\b");
#else
        BC = nhStr("\033[D");
#endif
        nh_HI = SO = nhStr("\033[1m");
        nh_US = nhStr("\033[4m");
        MR = nhStr("\033[7m");
        TI = nh_HE = ME = SE = nh_UE = nhStr("\033[0m");
        /* strictly, SE should be 2, and nh_UE should be 24,
           but we can't trust all ANSI emulators to be
           that complete.  -3. */
#ifndef MICRO
        AS = nhStr("\016");
        AE = nhStr("\017");
#endif
        TE = VS = VE = nullstr;
        init_hilite();
        *wid = CO;
        *hgt = LI;
        CL = nhStr("\033[2J"); /* last thing set */
        return;
    }
#endif /* TOS */
#endif /* ANSI_DEFAULT */

#ifdef TERMLIB
    tptr = (char *) alloc(1024);

    tbufptr = tbuf;
    if (!strncmp(term, "5620", 4))
        flags.null = FALSE; /* this should be a termcap flag */
    if (tgetent(tptr, term) < 1) {
        char buf[BUFSZ];
        (void) strncpy(buf, term,
                       (BUFSZ - 1) - (sizeof("Unknown terminal type: .  ")));
        buf[BUFSZ - 1] = '\0';
        error("Unknown terminal type: %s.", term);
    }
    if ((pc = Tgetstr(nhStr("pc"))) != 0)
        PC = *pc;

    if (!(BC = Tgetstr(nhStr("le")))) { /* both termcap and terminfo use le */
#ifdef TERMINFO
        error("Terminal must backspace.");
#else
        if (!(BC = Tgetstr(nhStr("bc")))) { /* termcap also uses bc/bs */
#ifndef MINIMAL_TERM
            if (!tgetflag(nhStr("bs")))
                error("Terminal must backspace.");
#endif
            BC = tbufptr;
            tbufptr += 2;
            *BC = '\b';
        }
#endif
    }

#ifdef MINIMAL_TERM
    HO = (char *) 0;
#else
    HO = Tgetstr(nhStr("ho"));
#endif
    /*
     * LI and CO are set in ioctl.c via a TIOCGWINSZ if available.  If
     * the kernel has values for either we should use them rather than
     * the values from TERMCAP ...
     */
#ifndef MICRO
    if (!CO)
        CO = tgetnum(nhStr("co"));
    if (!LI)
        LI = tgetnum(nhStr("li"));
#else
#if defined(TOS) && defined(__GNUC__)
    if (!strcmp(term, "builtin")) {
        get_scr_size();
    } else
#endif
    {
        CO = tgetnum(nhStr("co"));
        LI = tgetnum(nhStr("li"));
        if (!LI || !CO) /* if we don't override it */
            get_scr_size();
    }
#endif /* ?MICRO */
#ifdef CLIPPING
    if (CO < COLNO || LI < ROWNO + 3)
        setclipped();
#endif
    nh_ND = Tgetstr(nhStr("nd")); /* move cursor right 1 column */
    if (tgetflag(nhStr("os"))) /* term can overstrike */
        error("NetHack can't have OS.");
    if (tgetflag(nhStr("ul"))) /* underline by overstrike w/ underscore */
        ul_hack = TRUE;
    CE = Tgetstr(nhStr("ce")); /* clear line from cursor to eol */
    UP = Tgetstr(nhStr("up")); /* move cursor up 1 line */
    /* It seems that xd is no longer supported, and we should use
       a linefeed instead; unfortunately this requires resetting
       CRMOD, and many output routines will have to be modified
       slightly. Let's leave that till the next release. */
    XD = Tgetstr(nhStr("xd"));
    /* not:             XD = Tgetstr("do"); */
    if (!(nh_CM = Tgetstr(nhStr("cm")))) { /* cm: move cursor */
        if (!UP && !HO)
            error("NetHack needs CM or UP or HO.");
        tty_raw_print("Playing NetHack on terminals without CM is suspect.");
        tty_wait_synch();
    }
    SO = Tgetstr(nhStr("so")); /* standout start */
    SE = Tgetstr(nhStr("se")); /* standout end */
    nh_US = Tgetstr(nhStr("us")); /* underline start */
    nh_UE = Tgetstr(nhStr("ue")); /* underline end */
    ZH = Tgetstr(nhStr("ZH")); /* italic start */
    ZR = Tgetstr(nhStr("ZR")); /* italic end */
    SG = tgetnum(nhStr("sg")); /* -1: not fnd; else # of spaces left by so */
    if (!SO || !SE || (SG > 0))
        SO = SE = nh_US = nh_UE = nullstr;
    TI = Tgetstr(nhStr("ti")); /* nonconsequential cursor movement start */
    TE = Tgetstr(nhStr("te")); /* nonconsequential cursor movement end */
    VS = VE = nullstr;
#ifdef TERMINFO
    VS = Tgetstr(nhStr("eA")); /* enable graphics */
#endif
    KS = Tgetstr(nhStr("ks")); /* keypad start (special mode) */
    KE = Tgetstr(nhStr("ke")); /* keypad end (ordinary mode [ie, digits]) */
    MR = Tgetstr(nhStr("mr")); /* reverse */
    MB = Tgetstr(nhStr("mb")); /* blink */
    MD = Tgetstr(nhStr("md")); /* boldface */
    if (!SO)
        SO = MD;
    MH = Tgetstr(nhStr("mh")); /* dim */
    ME = Tgetstr(nhStr("me")); /* turn off all attributes */
    if (!ME)
        ME = SE ? SE : nullstr; /* default to SE value */

    nh_VI = Tgetstr(nhStr("vi"));
    nh_VE = Tgetstr(nhStr("ve"));
    /*nh_VS = Tgetstr(nhStr("vs"));*/
    if (!nh_VI || !nh_VE /*|| !nh_VS*/ )
        nh_VI = nh_VE = /*nh_VS =*/ (char *) 0;

    nh_Ic = Tgetstr(nhStr("Ic"));

    /* Get rid of padding numbers for nh_HI and nh_HE.  Hope they
     * aren't really needed!!!  nh_HI and nh_HE are outputted to the
     * pager as a string - so how can you send it NULs???
     *  -jsb
     */
    for (i = 0; digit(SO[i]); ++i)
        continue;
    nh_HI = dupstr(&SO[i]);
    for (i = 0; digit(ME[i]); ++i)
        continue;
    nh_HE = dupstr(&ME[i]);
    dynamic_HIHE = TRUE;

    AS = Tgetstr(nhStr("as")); /* alt charset start */
    AE = Tgetstr(nhStr("ae")); /* alt charset end */
    nh_CD = Tgetstr(nhStr("cd")); /* clear lines from cursor and down */
#if defined(TOS) && defined(__GNUC__)
    if (!strcmp(term, "builtin") || !strcmp(term, "tw52")
        || !strcmp(term, "st52")) {
        init_hilite();
    }
#else
    init_hilite();
#endif
    *wid = CO;
    *hgt = LI;
    /* cl: clear screen, set cursor to upper left */
    if (!(CL = Tgetstr(nhStr("cl")))) /* last thing set */
        error("NetHack needs CL.");
    if ((int) (tbufptr - tbuf) > (int) (sizeof tbuf))
        error("TERMCAP entry too big...\n");
    free((genericptr_t) tptr);
#endif /* TERMLIB */
    /* keep static copies of these so that raw_print_bold() will work
       after exit_nhwindows(); if the sequences are too long, then bold
       won't work after that--it will be rendered as ordinary text */
    if (nh_HI && strlen(nh_HI) < sizeof tty_standout_on)
        Strcpy(tty_standout_on, nh_HI);
    if (nh_HE && strlen(nh_HE) < sizeof tty_standout_off)
        Strcpy(tty_standout_off, nh_HE);
}

/* note: at present, this routine is not part of the formal window interface
 */
/* deallocate resources prior to final termination */
void
term_shutdown(void)
{
    /* we only attempt to clean up a few individual termcap variables */
#if defined(TERMLIB) || defined(ANSI_DEFAULT)
    kill_hilite();
#endif
#ifdef TERMLIB
    if (dynamic_HIHE) {
        free((genericptr_t) nh_HI), nh_HI = (char *) 0;
        free((genericptr_t) nh_HE), nh_HE = (char *) 0;
        dynamic_HIHE = FALSE;
    }
#endif
    return;
}

void
tty_number_pad(int state)
{
    switch (state) {
    case -1: /* activate keypad mode (escape sequences) */
        if (KS && *KS)
            xputs(KS);
        break;
    case 1: /* activate numeric mode for keypad (digits) */
        if (KE && *KE)
            xputs(KE);
        break;
    case 0: /* don't need to do anything--leave terminal as-is */
    default:
        break;
    }
}

#ifdef TERMLIB
extern void (*decgraphics_mode_callback)(void); /* defined in symbols.c */
static void tty_decgraphics_termcap_fixup(void);

/*
   We call this routine whenever DECgraphics mode is enabled, even if it
   has been previously set, in case the user manages to reset the fonts.
   The actual termcap fixup only needs to be done once, but we can't
   call xputs() from the option setting or graphics assigning routines,
   so this is a convenient hook.
 */
static void
tty_decgraphics_termcap_fixup(void)
{
    static char ctrlN[] = "\016";
    static char ctrlO[] = "\017";
    static char appMode[] = "\033=";
    static char numMode[] = "\033>";

    /* these values are missing from some termcaps */
    if (!AS)
        AS = ctrlN; /* ^N (shift-out [graphics font]) */
    if (!AE)
        AE = ctrlO; /* ^O (shift-in  [regular font])  */
    if (!KS)
        KS = appMode; /* ESC= (application keypad mode) */
    if (!KE)
        KE = numMode; /* ESC> (numeric keypad mode) */
    /*
     * Select the line-drawing character set as the alternate font.
     * Do not select NA ASCII as the primary font since people may
     * reasonably be using the UK character set.
     */
    if (SYMHANDLING(H_DEC)) {
        xputs("\033)0"); /* "\e)0" load line drawing chars as secondary set */
        /* TI doesn't necessarily do this; explicitly switch to primary
           font in case previous program (either before starting nethack
           or during a shell escape) left the alternate font active */
        xputs(AE);
    }

#ifdef PC9800
    init_hilite();
#endif

#if defined(ASCIIGRAPH) && !defined(NO_TERMS)
#if DECgraphicsOptimization
    /* some termcaps suffer from the bizarre notion that resetting
       video attributes should also reset the chosen character set */
    if (dynamic_HIHE) {
        assert(nh_HE != NULL);
        xputs(nh_HE); /* turn off any active highlighting (before maybe
                       * changing HE or AE) */
        (void) strsubst(nh_HE, AE, "");
        (void) strsubst(nh_HE, ctrlO, "");
        /* if AE has prefixing, substituting an empty string for it in HE
           would only work if it is a leading prefix of HE; remove the
           magic sequences that loads US set ("\e(B") or UK set ("\e(A")
           into the primary character set since we don't want HE to do that */
        (void) strsubst(nh_HE, "\033(B", "");
        (void) strsubst(nh_HE, "\033(A", "");
    }
#endif

    /* if AE is still present in HE, set a flag so that glyph writing
       code will know that AS needs to be refreshed for consecutive
       line drawing characters */
    const char *ae = AE;

    if (digit(*ae)) { /* skip over delay prefix, if any */
        do
            ++ae;
        while (digit(*ae));
        if (*ae == '.') {
            ++ae;
            if (digit(*ae))
                ++ae;
        }
        if (*ae == '*')
            ++ae;
    }
    /* stdc strstr(), not nethack's strstri(); HE ends color, ME ends
       inverse video; they might have the same value; sequences to end
       other attributes aren't known to sometimes contain AE */
    if ((nh_HE && strstr(nh_HE, ae)) || (ME && strstr(ME, ae)))
        HE_resets_AS = TRUE;

#ifdef DECgraphicsOptimization
    /* some termcaps have AS load the line-drawing character set as
       primary instead of having initialization load it as secondary
       (we've already done that init) and then having AS simply switch
       to secondary (change to do that now); they also have AE load
       the US character set, which we avoid by not touching primary;
       if HE_resets_AS, we can't simplify AS/AE due to the risk that
       HE is changing the primary set rather than just toggling to it */
    if (!HE_resets_AS && !strcmp(AS, "\033(0") && !strcmp(AE, "\033(B")) {
        /* first output old AE to make sure we aren't about to leave
           primary set with line drawing chars */
        xputs(AE);
        AS = ctrlN;
        AE = ctrlO;
    }
#endif /* DECgraphicsOptimization */
    xputs(AE);
#endif /* ASCIIGRAPH && !NO_TERMS */
}
#endif /* TERMLIB */

#if defined(ASCIIGRAPH) && defined(PC9800)
extern void (*ibmgraphics_mode_callback)(void); /* defined in symbols.c */
#endif
extern void (*utf8graphics_mode_callback)(void); /* defined in symbols.c */

#ifdef PC9800
extern void (*ascgraphics_mode_callback)(void); /* defined in symbols.c */
static void tty_ascgraphics_hilite_fixup(void);

static void
tty_ascgraphics_hilite_fixup(void)
{
    int c;

    for (c = 0; c < CLR_MAX / 2; c++)
        if (c != CLR_BLACK) {
            hilites[c | BRIGHT] = (char *) alloc(sizeof "\033[1;3%dm");
            Sprintf(hilites[c | BRIGHT], "\033[1;3%dm", c);
            if (c != CLR_GRAY) {
                hilites[c] = (char *) alloc(sizeof "\033[0;3%dm");
                Sprintf(hilites[c], "\033[0;3%dm", c);
            }
        }
}
#endif /* PC9800 */

void
term_start_screen(void)
{
#ifdef NOMUX_CAPTURE
    /* Returning to the tty screen means normal ttyDisplay cursor state is
     * authoritative again.  Startup/config raw_print() output may have
     * entered NOMUX raw shadow mode before windows were ready; do not let
     * that shadow cursor leak into later in-game input markers. */
    nomux_reset_raw_mode();
#endif
    xputs(TI);
    xputs(VS);
#ifdef PC9800
    if (!SYMHANDLING(H_IBM))
        tty_ascgraphics_hilite_fixup();
    /* set up callback in case option is not set yet but toggled later */
    ascgraphics_mode_callback = tty_ascgraphics_hilite_fixup;
#ifdef ASCIIGRAPH
    if (SYMHANDLING(H_IBM))
        init_hilite();
    /* set up callback in case option is not set yet but toggled later */
    ibmgraphics_mode_callback = init_hilite;
#endif
#endif /* PC9800 */

#ifdef TERMLIB
    if (SYMHANDLING(H_DEC))
        tty_decgraphics_termcap_fixup();
    /* set up callback in case option is not set yet but toggled later */
    decgraphics_mode_callback = tty_decgraphics_termcap_fixup;
#endif
#ifdef ENHANCED_SYMBOLS
    utf8graphics_mode_callback = tty_utf8graphics_fixup;
#endif

    if (gc.Cmd.num_pad)
        tty_number_pad(1); /* make keypad send digits */
}

void
term_end_screen(void)
{
    term_clear_screen();
    xputs(VE);
    xputs(TE);
}

/* Cursor movements */

/* Note to overlay tinkerers.  The placement of this overlay controls the
   location of the function xputc().  This function is not currently in
   trampoli.[ch] files for what is deemed to be performance reasons.  If
   this define is moved and or xputc() is taken out of the ROOT overlay,
   then action must be taken in trampoli.[ch]. */

void
nocmov(int x, int y)
{
    if ((int) ttyDisplay->cury > y) {
        if (UP) {
            while ((int) ttyDisplay->cury > y) { /* Go up. */
                xputs(UP);
                ttyDisplay->cury--;
            }
        } else if (nh_CM) {
            cmov(x, y);
        } else if (HO) {
            home();
            tty_curs(BASE_WINDOW, x + 1, y);
        } /* else impossible("..."); */
    } else if ((int) ttyDisplay->cury < y) {
        if (XD) {
            while ((int) ttyDisplay->cury < y) {
                xputs(XD);
                ttyDisplay->cury++;
            }
        } else if (nh_CM) {
            cmov(x, y);
        } else {
            while ((int) ttyDisplay->cury < y) {
                (void) xputc('\n');
                ttyDisplay->curx = 0;
                ttyDisplay->cury++;
            }
        }
    }
    if ((int) ttyDisplay->curx < x) { /* Go to the right. */
        if (!nh_ND) {
            cmov(x, y);
        } else { /* bah */
             /* should instead print what is there already */
            while ((int) ttyDisplay->curx < x) {
                xputs(nh_ND);
                ttyDisplay->curx++;
            }
        }
    } else if ((int) ttyDisplay->curx > x) {
        while ((int) ttyDisplay->curx > x) { /* Go to the left. */
            xputs(BC);
            ttyDisplay->curx--;
        }
    }
}

void
cmov(int x, int y)
{
    xputs(tgoto(nh_CM, x, y));
    ttyDisplay->cury = y;
    ttyDisplay->curx = x;
}

/* See note above.  xputc() is a special function for overlays. */
int
xputc(int c) /* actually char, but explicitly specify its widened type */
{
    /*
     * Note:  xputc() as a direct all to putchar() doesn't make any
     * sense _if_ putchar() is a function.  But if it is a macro, an
     * overlay configuration would want to avoid hidden code bloat
     * from multiple putchar() expansions.  And it gets passed as an
     * argument to tputs() so we have to guarantee an actual function
     * (while possibly lacking ANSI's (func) syntax to override macro).
     *
     * xputc() used to be declared as 'void xputc(c) char c; {}' but
     * avoiding the proper type 'int' just to avoid (void) casts when
     * ignoring the result can't have been sufficient reason to add it.
     * It also had '#if apollo' conditional to have the arg be int.
     * Matching putchar()'s declaration and using explicit casts where
     * warranted is more robust, so we're just a jacket around that.
     */
    return putchar(c);
}

#ifdef NOMUX_CAPTURE
/* In-band sync markers for screen recording.
 *
 * When NOMUX_MARKERS=1 is set in the environment, NetHack emits OSC
 * sequences to stdout at every input-wait boundary (and once at game
 * end after topten output), each carrying the rendered screen state
 * for that boundary.  A recorder reading stdout parses these markers
 * and uses the embedded screen as the captured frame for the step.
 *
 * Marker wire format:
 *   ESC ] 7777 ; KIND=input ; SEQ=N ; CX=cx ; CY=cy ; LOGPOS=P ; LEN=L BEL
 *   <L bytes of screen content>
 *
 * The OSC body up through BEL contains only printable ASCII so the
 * recorder can parse it unambiguously.  The L payload bytes that
 * follow are the rendered screen serialization (escape-sequences
 * preserved verbatim, no encoding) and may contain arbitrary bytes
 * including ESC.  The recorder reads exactly L bytes after the BEL.
 *
 * KIND=input fires at tty_nhgetch / tty_nh_poskey blocking points and
 * once at nh_terminate (post-topten final frame).  KIND=anim fires
 * for intermediate animation frames (zap beams, knockback, thrown
 * objects); these don't bump SEQ since they aren't input boundaries.
 *
 * When NOMUX_MARKERS is unset the emit is a no-op — the binary plays
 * normally with no extra stdout output. */

int
nomux_markers_enabled(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char *ev = harness_getenv("NOMUX_MARKERS");
        cached = (ev && ev[0] != '0') ? 1 : 0;
    }
    return cached;
}

static int nomux_seq = 0;
int nomux_anim_id = 0;
int nomux_in_child = 0; /* set in child() after fork (unixunix.c) */

/* Emit a single in-band marker carrying the current rendered screen.
 * KIND distinguishes input boundaries from animation frames. */
static void
nomux_emit_marker(const char *kind)
{
    if (!nomux_markers_enabled())
        return;

    {
        char *scr = nomux_capture_screen();
        int cx, cy;
        long logpos = rng_log_get_position();
        size_t scr_len = scr ? strlen(scr) : 0;
        nomux_get_cursor(&cx, &cy);
        fprintf(stdout, "\x1b]7777;KIND=%s;SEQ=%d;ANIM=%d;CX=%d;CY=%d;LOGPOS=%ld;LEN=%zu\x07",
                kind, nomux_seq, nomux_anim_id, cx, cy, logpos, scr_len);
        if (scr_len > 0)
            fwrite(scr, 1, scr_len, stdout);
        fflush(stdout);
    }
}

/* Called for intermediate animation frames: zap beams, hurtle/knockback
 * steps, thrown-object trajectories, etc.  Bumps the per-animation id
 * but NOT the input-boundary SEQ. */
void
nomux_capture_write_screen(void)
{
    nomux_anim_id++;
    nomux_emit_marker("anim");
}

/* Called at every real input-wait boundary (tty_nhgetch, tty_nh_poskey)
 * and once at nh_terminate after topten — the points where one
 * authoritative frame should be captured per step.  Bumps SEQ. */
void
nomux_capture_write_input_boundary(void)
{
    nomux_seq++;
    nomux_emit_marker("input");
}

/* NOMUX shadow frame buffer implementation */
nomux_cell nomux_buf[24][80];
static unsigned char nomux_fg_cur = 7;
/* Extended 256-colour foreground, stored as index+1; 0 means none. */
static unsigned int nomux_xfg_cur = 0;
static unsigned char nomux_attr_cur = 0;
static unsigned char nomux_decgfx_cur = 0;

void nomux_clear_screen(void) {
    int r, c;
    for (r = 0; r < 24; r++)
        for (c = 0; c < 80; c++) {
            nomux_buf[r][c].ch = ' ';
            nomux_buf[r][c].cp = 0;
            nomux_buf[r][c].xfg = 0;
            nomux_buf[r][c].fg = 7;
            nomux_buf[r][c].attr = 0; nomux_buf[r][c].decgfx = 0;
        }
}

void nomux_clear_to_eol(int row, int col) {
    int c;
    if (row < 0 || row >= 24) return;
    for (c = (col < 0 ? 0 : col); c < 80; c++) {
        nomux_buf[row][c].ch = ' ';
        nomux_buf[row][c].cp = 0;
        nomux_buf[row][c].fg = 7;
        nomux_buf[row][c].xfg = 0;
        nomux_buf[row][c].attr = 0;
        /* Reset decgfx too: cl_end's job is to wipe a region, and a
         * cleared cell shouldn't carry a graphics-mode flag from
         * whatever character occupied that position before. Without
         * this, leftover decgfx from the prior screen's map glyphs
         * (walls, floors) leaks into menu indent columns and shows
         * up in the wire as ESC-N space ESC-O around an otherwise
         * blank cell — visually identical to a regular space, but
         * not byte-equal across rebuilds or when comparing against
         * a JS port that clears decgfx on every overwrite. */
        nomux_buf[row][c].decgfx = 0;
    }
}

/* ── Topline UTF-8 column debt ─────────────────────────────────────────────
 * The message window writes RAW UTF-8 BYTES through topl_putsym(), which
 * advances ttyDisplay->curx once per BYTE (topl.c: the default arm of the
 * switch does a bare curx++ for every char handed to it).  A terminal
 * advances once per GLYPH, so from the first multi-byte glyph onward C's
 * curx runs (bytes - 1) columns ahead of the real cursor for the rest of
 * that line.
 *
 * That discrepancy is genuine upstream behaviour, not something to correct:
 * update_topl() word-wraps on strlen(bp) >= CO, so the inflated byte count
 * is what decides where the topline breaks and whether a --More-- appears.
 * But nomux_buf is a recording of the TERMINAL, so every cell write, every
 * clear-to-eol and the reported cursor on that line have to be shifted back
 * by the accumulated debt.
 *
 * This is NOT just the topline.  Menu and text windows take the same shape:
 * pager.c's look_all/look_traps/look_engrs prefix each line with
 * encglyph(glyph), tty_putmixed turns that into the customized glyph's raw
 * UTF-8 bytes, and wintty.c's window loops advance curx once per byte while
 * putchar()ing them one at a time.  So the accounting lives in nomux_putch
 * itself, gated on SYMHANDLING(H_UTF8), and every writer inherits it.
 *
 * H_IBM and H_DEC keep the plain byte path: a single high-bit CP437 or VT100
 * byte there must not be read as a UTF-8 lead, and 446 recorded sessions use
 * those sets.
 *
 * The debt resets when the row changes, at home(), and in tty_curs() -- but
 * only in tty_curs's MOVING arm.  Its `if (y == cy && x == cx) return;`
 * early exit emits nothing and leaves the terminal where it was, which is
 * exactly addtopl()'s `tty_curs(BASE_WINDOW, cw->curx + 1, cw->cury)`:
 * concatenated text really does land right after the glyph, so that arm has
 * to preserve the debt. */
static int nomux_u8_row = -1;         /* row the debt applies to */
static int nomux_u8_debt = 0;         /* columns curx leads the terminal by */
static unsigned int nomux_u8_acc = 0; /* codepoint under construction */
static int nomux_u8_pending = 0;      /* continuation bytes still due */
static int nomux_u8_start = 0;        /* terminal column of that codepoint */

void nomux_utf8_reset(void) {
    nomux_u8_row = -1;
    nomux_u8_debt = 0;
    nomux_u8_pending = 0;
}

int nomux_col_debt(int row) {
    return (row == nomux_u8_row) ? nomux_u8_debt : 0;
}

/* Write one cell.  cp == 0 records a byte, otherwise a codepoint (ch is
   left blank so any consumer still reading the byte field sees a space
   rather than a truncated UTF-8 fragment). */
static void nomux_putcell(int row, int col, int ch, unsigned int cp) {
    if (row < 0 || row >= 24 || col < 0 || col >= 80) return;
    nomux_buf[row][col].ch = cp ? ' ' : (char) ch;
    nomux_buf[row][col].cp = cp;
    nomux_buf[row][col].fg = nomux_fg_cur;
    nomux_buf[row][col].xfg = nomux_xfg_cur;
    nomux_buf[row][col].attr = nomux_attr_cur;
    nomux_buf[row][col].decgfx = nomux_decgfx_cur;
}

/* Reassemble a UTF-8 byte run into one cell and track the debt.  `col` is
   the column C wrote at; callers that have already advanced curx pass the
   corrected value the same way they always did. */
static void nomux_utf8_putch(int ch, int row, int col) {
    unsigned char c = (unsigned char) ch;

    if (row != nomux_u8_row) {
        nomux_u8_row = row;
        nomux_u8_debt = 0;
        nomux_u8_pending = 0;
    }
    if (col < 0) return;                 /* '\n': bookkeeping only */
    if (nomux_u8_pending > 0 && (c & 0xC0) == 0x80) {
        nomux_u8_acc = (nomux_u8_acc << 6) | (c & 0x3F);
        nomux_u8_debt++;
        if (--nomux_u8_pending == 0)
            nomux_putcell(row, nomux_u8_start, ' ', nomux_u8_acc);
        return;
    }
    nomux_u8_pending = 0;
    if (c >= 0xC2) {
        nomux_u8_start = col - nomux_u8_debt;
        if ((c & 0xE0) == 0xC0) { nomux_u8_acc = c & 0x1F; nomux_u8_pending = 1; }
        else if ((c & 0xF0) == 0xE0) { nomux_u8_acc = c & 0x0F; nomux_u8_pending = 2; }
        else if ((c & 0xF8) == 0xF0) { nomux_u8_acc = c & 0x07; nomux_u8_pending = 3; }
        if (nomux_u8_pending) return;
    }
    if (ch >= 32) nomux_putcell(row, col - nomux_u8_debt, c, 0);
}

void nomux_putch(int ch) {
    int row, col;
    if (!ttyDisplay) return;
    row = ttyDisplay->cury;
    col = ttyDisplay->curx;
    if (SYMHANDLING(H_UTF8)) {
        nomux_utf8_putch(ch, row, col);
        return;
    }
    if (ch < 32) return;
    /* A non-UTF8 writer on the debt row ends that run. */
    if (row == nomux_u8_row) nomux_utf8_reset();
    nomux_putcell(row, col, ch, 0);
}

/* Record a Unicode codepoint in the cell under the cursor.
   Mirrors nomux_putch: the position comes from ttyDisplay and the CALLER
   advances curx, because on the MAP one codepoint occupies one column
   exactly as one byte does (g_pututf8 is called once per column). */
void nomux_putcp(unsigned int cp) {
    if (!ttyDisplay || !cp) return;
    if (ttyDisplay->cury == nomux_u8_row) nomux_utf8_reset();
    nomux_putcell(ttyDisplay->cury, ttyDisplay->curx, ' ', cp);
}


void nomux_set_attr(int attr) {
    switch (attr) {
    case ATR_INVERSE: nomux_attr_cur |= 1; break;
    case ATR_BOLD:    nomux_attr_cur |= 2; break;
    case ATR_ULINE:   nomux_attr_cur |= 4; break;
    }
}
void nomux_end_attr(void) { nomux_attr_cur = 0; }
void nomux_set_fg(int color) {
    /* Mirror C's wc2_darkgray remap: when that option is on,
     * hilites[CLR_BLACK] is wired to BRIGHT_BLACK so the terminal
     * receives [90m instead of [30m (plain black is invisible
     * against a black background).  The shadow buffer must track
     * the *visible* color, not the nominal index, or the capture
     * will diverge from what the terminal (and JS) sees. */
    if (color == CLR_BLACK && iflags.wc2_darkgray)
        color = 8;  /* index 8 -> ANSI 90m (bright black / dark gray) */
    nomux_fg_cur = (color < 0 || color >= 16) ? 7 : (unsigned char) color;
    nomux_xfg_cur = 0;
}
void nomux_end_fg(void) { nomux_fg_cur = 7; nomux_xfg_cur = 0; }

/* term_start_extracolor() picks an exact colour; it supersedes any basic one
   until the next term_start_color()/term_end_color(). */
void nomux_set_extracolor(unsigned int idx256) { nomux_xfg_cur = idx256 + 1; }

/* term_end_extracolor() emits ESC[0m, which resets the terminal to its
   default foreground, so the shadow buffer has to drop the exact colour too.
   Without this nomux_xfg_cur stayed set after the last customized glyph of a
   frame and every cell written afterwards inherited it -- most visibly the
   STATUS line, which recorded as `ESC[38;5;33m HP:13 ESC[39m` under Enhanced2
   while a real terminal (and JS) showed it plain. */
void nomux_end_extracolor(void) { nomux_xfg_cur = 0; }

static int nomux_sgr_fg(int fg) {
    if (fg >= 0 && fg <= 7) return 30 + fg;
    if (fg >= 8 && fg <= 15) return 90 + (fg - 8);
    return 37;
}

/* Full-map colored captures can be much larger than a plain 24x80 screen:
 * each cell may carry SGR and DEC graphics transitions.  Gehennom water/lava
 * maps with dark-gray recoloring exceeded the old 24*256 buffer, corrupting
 * subsequent OSC marker headers into the payload.  Budget 32 bytes/cell plus
 * row separators/resets. */
static char nomux_out[(24 * 80 * 32) + (24 * 16) + 1];
char *nomux_capture_screen(void) {
    char *p = nomux_out;
    int row, col, end, cur_fg, cur_attr;
    unsigned int cur_xfg;
    /* tmux capture-pane -e format:
     * - Color start: \033[97m (just the code, no 0; prefix)
     * - Color end (to default): \033[39m (fg reset only)
     * - Attr start: \033[7m (inverse), \033[1m (bold)
     * - Attr end: \033[27m (end inverse), \033[22m (end bold)
     * - Full reset: \033[0m only at end-of-line or when both attr+color reset
     * Incremental: only emit what changes between cells. */
    for (row = 0; row < 24; row++) {
        cur_fg = 7; cur_attr = 0; cur_xfg = 0;
        end = 79;
        while (end >= 0
               && nomux_buf[row][end].cp == 0
               && (nomux_buf[row][end].ch == ' ' || nomux_buf[row][end].ch == 0)
               && nomux_buf[row][end].attr == 0
               && (nomux_buf[row][end].fg == 7 || nomux_buf[row][end].fg == 0))
            end--;
        int in_dec = 0;
        for (col = 0; col <= end; col++) {
            nomux_cell *c = &nomux_buf[row][col];
            char ch = c->ch ? c->ch : ' ';
            int fg = c->fg ? c->fg : 7;
            unsigned int xfg = c->xfg;
            int at = c->attr;
            /* Emit incremental SGR changes */
            if (at != cur_attr) {
                /* Handle attr changes individually */
                if ((at & 1) && !(cur_attr & 1)) p += sprintf(p, "\033[7m");
                if (!(at & 1) && (cur_attr & 1)) p += sprintf(p, "\033[27m");
                if ((at & 2) && !(cur_attr & 2)) p += sprintf(p, "\033[1m");
                if (!(at & 2) && (cur_attr & 2)) p += sprintf(p, "\033[22m");
                if ((at & 4) && !(cur_attr & 4)) p += sprintf(p, "\033[4m");
                if (!(at & 4) && (cur_attr & 4)) p += sprintf(p, "\033[24m");
                cur_attr = at;
            }
            /* An exact colour supersedes the basic index for this cell.
               Emit the same SGR the terminal received (38;5;N); leaving
               cur_fg alone makes the next basic-coloured cell re-emit. */
            if (xfg != cur_xfg) {
                if (xfg) {
                    p += sprintf(p, "\033[38;5;%um", xfg - 1);
                } else {
                    p += sprintf(p, "\033[39m");
                    cur_fg = 7;
                }
                cur_xfg = xfg;
            }
            if (!xfg && fg != cur_fg) {
                if (fg == 7) {
                    p += sprintf(p, "\033[39m"); /* reset fg to default */
                } else {
                    p += sprintf(p, "\033[%dm", nomux_sgr_fg(fg));
                }
                cur_fg = fg;
            }
            if (c->decgfx && !in_dec) { *p++ = '\016'; in_dec = 1; }
            else if (!c->decgfx && in_dec) { *p++ = '\017'; in_dec = 0; }
            if (c->cp) {
                /* UTF-8 encode; one cell, one column, however many bytes. */
                unsigned int cp = c->cp;
                if (cp < 0x80) {
                    *p++ = (char) cp;
                } else if (cp < 0x800) {
                    *p++ = (char) (0xC0 | (cp >> 6));
                    *p++ = (char) (0x80 | (cp & 0x3F));
                } else if (cp < 0x10000) {
                    *p++ = (char) (0xE0 | (cp >> 12));
                    *p++ = (char) (0x80 | ((cp >> 6) & 0x3F));
                    *p++ = (char) (0x80 | (cp & 0x3F));
                } else {
                    *p++ = (char) (0xF0 | (cp >> 18));
                    *p++ = (char) (0x80 | ((cp >> 12) & 0x3F));
                    *p++ = (char) (0x80 | ((cp >> 6) & 0x3F));
                    *p++ = (char) (0x80 | (cp & 0x3F));
                }
            } else {
                *p++ = ch;
            }
        }
        if (in_dec) { *p++ = '\017'; in_dec = 0; }
        /* End-of-line: reset to defaults — match tmux format */
        if (cur_attr && cur_fg != 7) {
            p += sprintf(p, "\033[0m");  /* both attr+color: full reset */
        } else if (cur_attr) {
            p += sprintf(p, "\033[0m");  /* attr only: full reset */
        } else if (cur_fg != 7) {
            p += sprintf(p, "\033[39m"); /* color only: fg reset */
            cur_fg = 7; cur_attr = 0;
        }
        if (row < 23) *p++ = '\n';
    }
    *p = '\0';
    return nomux_out;
}

/* Raw-mode shadow tracking for tty_raw_print / tty_raw_print_bold output.
 * The C code calls these directly via puts() and fputs(), bypassing the
 * normal wintty rendering pipeline (no tty_putsym/tty_putstr indirection).
 * The default end-of-game high-score panel (topten() with iflags.toptenwin
 * == FALSE) goes through this path entirely, so without these hooks the
 * NOMUX shadow buffer is blind to it.
 *
 * Raw mode is entered on the first raw_print after ttyDisplay is gone
 * (typical post-exit_nhwindows endgame sequence) or by an explicit call
 * to nomux_enter_raw_mode(). It uses its own row/col counters because
 * ttyDisplay is null at this point.
 */
static int nomux_raw_active = 0;
static int nomux_raw_row = 0;
static int nomux_raw_col = 0;

void nomux_enter_raw_mode(void) {
    nomux_raw_active = 1;
    nomux_raw_row = 0;
    nomux_raw_col = 0;
    /* exit_nhwindows / settty switch off alt-screen, so the visible
     * terminal returns to a blank main-screen state before the high-score
     * lines print. Mirror that by clearing the shadow. */
    nomux_clear_screen();
}

void nomux_reset_raw_mode(void) {
    nomux_raw_active = 0;
    nomux_raw_row = 0;
    nomux_raw_col = 0;
}

static void nomux_raw_putch(int ch) {
    if (ch == '\n') {
        nomux_raw_row++;
        nomux_raw_col = 0;
        return;
    }
    if (ch < 32) return;
    if (nomux_raw_row >= 0 && nomux_raw_row < 24
        && nomux_raw_col >= 0 && nomux_raw_col < 80) {
        nomux_buf[nomux_raw_row][nomux_raw_col].ch = (char) ch;
        nomux_buf[nomux_raw_row][nomux_raw_col].fg = nomux_fg_cur;
        nomux_buf[nomux_raw_row][nomux_raw_col].attr = nomux_attr_cur;
        nomux_buf[nomux_raw_row][nomux_raw_col].decgfx = nomux_decgfx_cur;
    }
    nomux_raw_col++;
}

/* Write a string emitted by tty_raw_print / tty_raw_print_bold to the
 * shadow buffer, then bump SEQ so the harness re-reads the file.
 * If raw mode is not yet active and ttyDisplay is gone, enter raw mode
 * automatically (typical endgame path).
 *
 * Under NOMUX_MARKERS we have no tmux fallback, so we ALSO enter raw
 * mode when ttyDisplay is still alive — otherwise tty_suspend_nhwindows
 * goodbye strings ("Be seeing you...", "Saving...") and topten
 * output via raw_print arrive before ttyDisplay is freed and would
 * never reach the shadow buffer. The raw-print path is reserved for
 * end-of-game / panic output, so clearing the screen here is the
 * right behavior. */
void nomux_raw_emit(const char *s, int bold) {
    if (!nomux_raw_active) {
        if (ttyDisplay && (!*s || !nomux_markers_enabled()))
            return;  /* in-game flush, or no-marker mode → skip */
        nomux_enter_raw_mode();
    }
    if (bold) nomux_attr_cur |= 2;
    for (const char *p = s; *p; p++)
        nomux_raw_putch((int)(unsigned char)*p);
    nomux_raw_putch('\n');  /* puts() / tty_raw_print_bold both append a newline */
    if (bold) nomux_attr_cur = 0;
    /* INTENTIONALLY do NOT call nomux_capture_write_input_boundary() here.
     *
     * The Python harness side (run_session.py:wait_for_nomux_advance)
     * waits for SEQ to advance past prev_seq, then reads the screen file.
     * If raw_emit bumps SEQ on EVERY call, the harness sees SEQ advance
     * after the FIRST emit and proceeds to read the screen — capturing
     * INTERMEDIATE topten output (e.g. blank-only-emitted, before
     * "You made the top ten list!" is written). This produced a
     * non-deterministic 1-blank vs 2-blank pattern in seed0030 segs
     * 1/2/5/7 (issue #460). Adding a tiny I/O delay here (an fopen+fclose
     * for debug) was enough to consistently shift to 1-blank for all
     * segments — confirming the race.
     *
     * Letting the next tty_nhgetch / tty_nh_poskey call's
     * nomux_capture_write_input_boundary fire is not enough at game-end
     * because there's no further input wait after topten — the next
     * thing is nh_terminate(). So we add a single write at nh_terminate
     * (see src/end.c) to capture the final state once topten has
     * finished emitting. */
}

void nomux_raw_emit_eol(void) {
    if (nomux_raw_active) nomux_raw_putch('\n');
}

void nomux_get_cursor(int *cx, int *cy) {
    if (ttyDisplay) {
        /* The terminal's cursor, not C's byte-counting curx. */
        *cx = ttyDisplay->curx - nomux_col_debt(ttyDisplay->cury);
        *cy = ttyDisplay->cury;
        return;
    }
    if (nomux_raw_active) {
        *cx = nomux_raw_col;
        *cy = nomux_raw_row;
        return;
    }
    *cx = 0;
    *cy = 0;
}
#endif /* NOMUX_CAPTURE */

void
xputs(const char *s)
{
#ifndef TERMLIB
    (void) fputs(s, stdout);
#else
    tputs(s, 1, xputc);
#endif
}

void
cl_end(void)
{
    if (CE) {
        xputs(CE);
#ifdef NOMUX_CAPTURE
        nomux_clear_to_eol(ttyDisplay->cury,
                           ttyDisplay->curx - nomux_col_debt(ttyDisplay->cury));
#endif
    } else { /* no-CE fix - free after Harold Rynes */
        int cx = ttyDisplay->curx + 1;

        /* this looks terrible, especially on a slow terminal
           but is better than nothing */
        while (cx < CO) {
            (void) xputc(' ');
            cx++;
        }
        tty_curs(BASE_WINDOW, (int) ttyDisplay->curx + 1,
                 (int) ttyDisplay->cury);
    }
}

void
term_clear_screen(void)
{
    /* note: if CL is null, then termcap initialization failed,
     * so don't attempt screen-oriented I/O during final cleanup.
     */
    if (CL) {
        xputs(CL);
        home();
        /* set remembered data to all spaces */
#ifdef NOMUX_CAPTURE
        nomux_clear_screen();
#endif
        erase_tty_screen();
    }
}

void
home(void)
{
    if (HO)
        xputs(HO);
    else if (nh_CM)
        xputs(tgoto(nh_CM, 0, 0));
    else
        tty_curs(BASE_WINDOW, 1, 0); /* using UP ... */
    ttyDisplay->curx = ttyDisplay->cury = 0;
#ifdef NOMUX_CAPTURE
    nomux_utf8_reset();
#endif
}

void
standoutbeg(void)
{
    if (SO) {
        xputs(SO);
#ifdef NOMUX_CAPTURE
        /* standout is reverse video on the recording terminal
           (xterm-256color smso); track it like term_start_attr */
        nomux_set_attr(ATR_INVERSE);
#endif
    }
}

void
standoutend(void)
{
    if (SE) {
        xputs(SE);
#ifdef NOMUX_CAPTURE
        nomux_end_attr();
#endif
    }
}

#if 0 /* if you need one of these, uncomment it (here and in extern.h) */
void
revbeg(void)
{
    if (MR)
        xputs(MR);
}

void
boldbeg(void)
{
    if (MD)
        xputs(MD);
}

void
blinkbeg(void)
{
    if (MB)
        xputs(MB);
}

void
dimbeg(void)
{
    /* not in most termcap entries */
    if (MH)
        xputs(MH);
}

void
m_end(void)
{
    if (ME)
        xputs(ME);
}
#endif /*0*/

void
backsp(void)
{
    xputs(BC);
}

void
tty_nhbell(void)
{
    if (flags.silent)
        return;
    (void) putchar('\007'); /* curx does not change */
    (void) fflush(stdout);
}

#ifdef ASCIIGRAPH
void
graph_on(void)
{
#ifdef NOMUX_CAPTURE
    nomux_decgfx_cur = 1;
#endif
    if (AS)
        xputs(AS);
}

void
graph_off(void)
{
#ifdef NOMUX_CAPTURE
    nomux_decgfx_cur = 0;
#endif
    if (AE)
        xputs(AE);
}
#endif

#if !defined(MICRO)
#ifdef VMS
static const short tmspc10[] = { /* from termcap */
                                 0, 2000, 1333, 909, 743, 666, 333, 166, 83,
                                 55, 50, 41, 27, 20, 13, 10, 5
};
#else
static const short tmspc10[] = { /* from termcap */
                                 0, 2000, 1333, 909, 743, 666, 500, 333, 166,
                                 83, 55, 41, 20, 10, 5
};
#endif
#endif

/* delay 50 ms */
void
tty_delay_output(void)
{
#if defined(MICRO)
    int i;
#endif
    /* Under NOMUX_MARKERS the harness skips the delay and captures the
       frame instead, so animations are recorded rather than slept. */
    if (nomux_markers_enabled() || iflags.debug_fuzzer) {
        (void) fflush(stdout);
#ifdef NOMUX_CAPTURE
        /* Capture the current screen at this animation boundary.
           This lets the session recorder see intermediate display
           states (zap beams, thrown objects, explosions) that would
           otherwise be invisible because the delay is suppressed.
           nomux_capture_write_screen appends an `anim` marker and
           bumps nomux_anim_id, which we include in the event marker
           so the appendix entry can be cross-referenced. */
        nomux_capture_write_screen();
        event_log("anim_frame[id=%d]", nomux_anim_id);
#endif
        return;
    }
#ifdef TIMED_DELAY
    if (flags.nap) {
        (void) fflush(stdout);
        msleep(50); /* sleep for 50 milliseconds */
        return;
    }
#endif
#if defined(MICRO)
    /* simulate the delay with "cursor here" */
    for (i = 0; i < 3; i++) {
        cmov(ttyDisplay->curx, ttyDisplay->cury);
        (void) fflush(stdout);
    }
#else /* MICRO */
    /* BUG: if the padding character is visible, as it is on the 5620
       then this looks terrible. */
    if (flags.null) {
        tputs(
#ifdef TERMINFO
              "$<50>",
#else
              "50",
#endif
              1, xputc);

    } else if (ospeed > 0 && ospeed < SIZE(tmspc10) && nh_CM) {
        /* delay by sending cm(here) an appropriate number of times */
        int cmlen =
            (int) strlen(tgoto(nh_CM, ttyDisplay->curx, ttyDisplay->cury));
        int i = 500 + tmspc10[ospeed] / 2;

        while (i > 0) {
            cmov((int) ttyDisplay->curx, (int) ttyDisplay->cury);
            i -= cmlen * tmspc10[ospeed];
        }
    }
#endif /* MICRO */
}

/* must only be called with curx = 1 */
void
cl_eos(void) /* free after Robert Viduya */
{
    if (nh_CD) {
        xputs(nh_CD);
#ifdef NOMUX_CAPTURE
        { int r; nomux_clear_to_eol(ttyDisplay->cury, ttyDisplay->curx - nomux_col_debt(ttyDisplay->cury)); for (r = ttyDisplay->cury + 1; r < 24; r++) nomux_clear_to_eol(r, 0); }
#endif
    } else {
        int cy = ttyDisplay->cury + 1;

        while (cy <= LI - 2) {
            cl_end();
            (void) xputc('\n');
            cy++;
        }
        cl_end();
        tty_curs(BASE_WINDOW, (int) ttyDisplay->curx + 1,
                 (int) ttyDisplay->cury);
    }
}

#if defined(TERMLIB)
#if defined(UNIX) && defined(TERMINFO)
/*
 * Sets up color highlighting, using terminfo(4) escape sequences.
 *
 * Having never seen a terminfo system without curses, we assume this
 * inclusion is safe.  On systems with color terminfo, it should define
 * the 8 COLOR_FOOs, and avoid us having to guess whether this particular
 * terminfo uses BGR or RGB for its indexes.
 *
 * If we don't get the definitions, then guess.  Original color terminfos
 * used BGR for the original Sf (setf, Standard foreground) codes, but
 * there was a near-total lack of user documentation, so some subsequent
 * terminfos, such as early Linux ncurses and SCO UNIX, used RGB.  Possibly
 * as a result of the confusion, AF (setaf, ANSI Foreground) codes were
 * introduced, but this caused yet more confusion.  Later Linux ncurses
 * have BGR Sf, RGB AF, and RGB COLOR_FOO, which appears to be the SVR4
 * standard.  We could switch the colors around when using Sf with ncurses,
 * which would help things on later ncurses and hurt things on early ncurses.
 * We'll try just preferring AF and hoping it always agrees with COLOR_FOO,
 * and falling back to Sf if AF isn't defined.
 *
 * In any case, treat black specially so we don't try to display black
 * characters on the assumed black background.
 */

/* `curses' is aptly named; various versions don't like these
    macros used elsewhere within nethack; fortunately they're
    not needed beyond this point, so we don't need to worry
    about reconstructing them after the header file inclusion. */
#undef TRUE
#undef FALSE
#define m_move curses_m_move /* some curses.h decl m_move(), not used here */

#include <curses.h>

#if defined(TPARM_WORKAROUND)
extern char *tparm();
#endif

#ifndef COLOR_BLACK /* trust include file */
#ifndef _M_UNIX     /* guess BGR */
#define COLOR_BLACK 0
#define COLOR_BLUE 1
#define COLOR_GREEN 2
#define COLOR_CYAN 3
#define COLOR_RED 4
#define COLOR_MAGENTA 5
#define COLOR_YELLOW 6
#define COLOR_WHITE 7
#else /* guess RGB */
#define COLOR_BLACK 0
#define COLOR_RED 1
#define COLOR_GREEN 2
#define COLOR_YELLOW 3
#define COLOR_BLUE 4
#define COLOR_MAGENTA 5
#define COLOR_CYAN 6
#define COLOR_WHITE 7
#endif
#endif

/* Mapping data for the six terminfo colors that resolve to pairs of nethack
 * colors.  Black and white are handled specially.
 */
const struct {
    int ti_color, nh_color, nh_bright_color;
} ti_map[6] = { { COLOR_RED, CLR_RED, CLR_ORANGE },
                { COLOR_GREEN, CLR_GREEN, CLR_BRIGHT_GREEN },
                { COLOR_YELLOW, CLR_BROWN, CLR_YELLOW },
                { COLOR_BLUE, CLR_BLUE, CLR_BRIGHT_BLUE },
                { COLOR_MAGENTA, CLR_MAGENTA, CLR_BRIGHT_MAGENTA },
                { COLOR_CYAN, CLR_CYAN, CLR_BRIGHT_CYAN } };

typedef struct {
    unsigned char r, g, b;
} RGB;

static char nilstring[] = "";

static void
init_hilite(void)
{
    int c, colors;
    char *setf, *scratch;

    colors = tgetnum(nhStr("Co"));
    iflags.colorcount = colors;
#ifdef NCURSES_VERSION
    /* The standard ncurses terminfo entries that support 24-bit colors
       (*-direct) map the standard 8/16/256 colors onto the beginning
       of the 24-bit color range. For example, rgb(0,0,1) appears as red
       instead of nearly black. iflags.colorcount should retain the actual
       supported color count, while for default color initialization we
       take the available indexed colors into consieration. */
    colors = indexed_color_count();
#endif
    int md_len = 0;

    if (colors < 8 || !MD || !*MD
        || ((setf = tgetstr(nhStr("AF"), (char **) 0)) == (char *) 0
            && (setf = tgetstr(nhStr("Sf"), (char **) 0)) == (char *) 0)) {
        /* Fallback when colors not available
         * It's arbitrary to collapse all colors except gray
         * together, but that's what the previous code did.
         */
        hilites[CLR_BLACK] = nh_HI;
        hilites[CLR_RED] = nh_HI;
        hilites[CLR_GREEN] = nh_HI;
        hilites[CLR_BROWN] = nh_HI;
        hilites[CLR_BLUE] = nh_HI;
        hilites[CLR_MAGENTA] = nh_HI;
        hilites[CLR_CYAN] = nh_HI;
        hilites[CLR_GRAY] = nilstring;
        hilites[NO_COLOR] = nilstring;
        hilites[CLR_ORANGE] = nh_HI;
        hilites[CLR_BRIGHT_GREEN] = nh_HI;
        hilites[CLR_YELLOW] = nh_HI;
        hilites[CLR_BRIGHT_BLUE] = nh_HI;
        hilites[CLR_BRIGHT_MAGENTA] = nh_HI;
        hilites[CLR_BRIGHT_CYAN] = nh_HI;
        hilites[CLR_WHITE] = nh_HI;
        return;
    }

    if (colors >= 16) {
        for (c = 0; c < SIZE(ti_map); c++) {
            /* system colors */
            scratch = tparm2(setf, ti_map[c].nh_color);
            hilites[ti_map[c].nh_color] = dupstr(scratch);
            /* bright colors */
            scratch = tparm2(setf, ti_map[c].nh_bright_color);
            hilites[ti_map[c].nh_bright_color] = dupstr(scratch);
        }
    } else {
        /* 8 system colors */
        md_len = (int) strlen(MD);

        c = 6;
        while (c--) {
            char *work;

            scratch = tparm2(setf, ti_map[c].ti_color);
            work = (char *) alloc(strlen(scratch) + md_len + 1);
            Strcpy(work, MD);
            hilites[ti_map[c].nh_bright_color] = work;
            work += md_len;
            Strcpy(work, scratch);
            hilites[ti_map[c].nh_color] = work;
        }
    }

    if (colors >= 16) {
        scratch = tparm2(setf, COLOR_WHITE | BRIGHT);
        hilites[CLR_WHITE] = dupstr(scratch);
    } else {
        scratch = tparm2(setf, COLOR_WHITE);
        hilites[CLR_WHITE] = (char *) alloc(strlen(scratch) + md_len + 1);
        Strcpy(hilites[CLR_WHITE], MD);
        Strcat(hilites[CLR_WHITE], scratch);
    }

    hilites[CLR_GRAY] = nilstring;
    hilites[NO_COLOR] = nilstring;

    if (iflags.wc2_darkgray) {
        if (colors >= 16) {
            scratch = tparm2(setf, COLOR_BLACK|BRIGHT);
            hilites[CLR_BLACK] = dupstr(scratch);
        } else {
            /* On many terminals, esp. those using classic PC CGA/EGA/VGA
            * textmode, specifying "hilight" and "black" simultaneously
            * produces a dark shade of gray that is visible against a
            * black background.  We can use it to represent black objects.
            */
            scratch = tparm2(setf, COLOR_BLACK);
            hilites[CLR_BLACK] = (char *) alloc(strlen(scratch) + md_len + 1);
            Strcpy(hilites[CLR_BLACK], MD);
            Strcat(hilites[CLR_BLACK], scratch);
        }
    } else {
        /* But it's conceivable that hilighted black-on-black could
         * still be invisible on many others.  We substitute blue for
         * black.
         */
        hilites[CLR_BLACK] = hilites[CLR_BLUE];
    }
}

static void
kill_hilite(void)
{
    int c;

    /* if colors weren't available, no freeing needed */
    if (hilites[CLR_BLACK] == nh_HI)
        return;

    /* hilites[] will be set to NULL below, whether freed here or not */

    if (hilites[CLR_BLACK]) {
        if (hilites[CLR_BLACK] != hilites[CLR_BLUE])
            free(hilites[CLR_BLACK]), hilites[CLR_BLACK] = NULL;
    }
    if (indexed_color_count() >= 16) {
        if (hilites[CLR_BLUE])
            free(hilites[CLR_BLUE]);
        if (hilites[CLR_GREEN])
            free(hilites[CLR_GREEN]);
        if (hilites[CLR_CYAN])
            free(hilites[CLR_CYAN]);
        if (hilites[CLR_MAGENTA])
            free(hilites[CLR_MAGENTA]);
        if (hilites[CLR_RED])
            free(hilites[CLR_RED]);
        if (hilites[CLR_BROWN])
            free(hilites[CLR_BROWN]);
    } else {
        /* CLR_BLUE overlaps CLR_BRIGHT_BLUE, do not free */
        /* CLR_GREEN overlaps CLR_BRIGHT_GREEN, do not free */
        /* CLR_CYAN overlaps CLR_BRIGHT_CYAN, do not free */
        /* CLR_MAGENTA overlaps CLR_BRIGHT_MAGENTA, do not free */
        /* CLR_RED overlaps CLR_ORANGE, do not free */
        /* CLR_BROWN overlaps CLR_YELLOW, do not free */
    }
    /* CLR_GRAY is static 'nilstring', do not free */
    /* NO_COLOR is static 'nilstring', do not free */
    if (hilites[CLR_BRIGHT_BLUE])
        free(hilites[CLR_BRIGHT_BLUE]);
    if (hilites[CLR_BRIGHT_GREEN])
        free(hilites[CLR_BRIGHT_GREEN]);
    if (hilites[CLR_BRIGHT_CYAN])
        free(hilites[CLR_BRIGHT_CYAN]);
    if (hilites[CLR_BRIGHT_MAGENTA])
        free(hilites[CLR_BRIGHT_MAGENTA]);
    if (hilites[CLR_ORANGE])
        free(hilites[CLR_ORANGE]);
    if (hilites[CLR_YELLOW])
        free(hilites[CLR_YELLOW]);
    if (hilites[CLR_WHITE])
        free(hilites[CLR_WHITE]);

    for (c = 0; c < CLR_MAX; c++)
        hilites[c] = NULL;
}

static int
indexed_color_count(void)
{
#ifdef NCURSES_VERSION
    /* ncurses adds the non-standard attribute CO for "number of indexed
       colors overlaying RGB space". */
    int idx_colors = tgetnum(nhStr("CO"));
    if (idx_colors > 0) {
        return idx_colors;
    }
#endif

    return tgetnum(nhStr("Co"));
}

#else /* UNIX && TERMINFO */

#ifndef TOS
/* find the foreground and background colors set by nh_HI or nh_HE */
static void
analyze_seq(char *str, int *fg, int *bg)
{
    int c, code;
    int len;

#ifdef MICRO
    *fg = CLR_GRAY;
    *bg = CLR_BLACK;
#else
    *fg = *bg = NO_COLOR;
#endif

    c = (str[0] == '\233') ? 1 : 2; /* index of char beyond esc prefix */
    len = strlen(str) - 1;          /* length excluding attrib suffix */
    if ((c != 1 && (str[0] != '\033' || str[1] != '[')) || (len - c) < 1
        || str[len] != 'm')
        return;

    while (c < len) {
        if ((code = atoi(&str[c])) == 0) { /* reset */
            /* this also catches errors */
#ifdef MICRO
            *fg = CLR_GRAY;
            *bg = CLR_BLACK;
#else
            *fg = *bg = NO_COLOR;
#endif
        } else if (code == 1) { /* bold */
            *fg |= BRIGHT;
#if 0
        /* I doubt we'll ever resort to using blinking characters,
           unless we want a pulsing glow for something.  But, in case
           we do... -3. */
        } else if (code == 5) { /* blinking */
            *fg |= BLINK;
        } else if (code == 25) { /* stop blinking */
            *fg &= ~BLINK;
#endif
        } else if (code == 7 || code == 27) { /* reverse */
            code = *fg & ~BRIGHT;
            *fg = *bg | (*fg & BRIGHT);
            *bg = code;
        } else if (code >= 30 && code <= 37) { /* hi_foreground RGB */
            *fg = code - 30;
        } else if (code >= 40 && code <= 47) { /* hi_background RGB */
            *bg = code - 40;
        }
        while (digit(str[++c]))
            ;
        c++;
    }
}
#endif

/*
 * Sets up highlighting sequences, using ANSI escape sequences (highlight code
 * found in wintty.c).  The nh_HI and nh_HE sequences (usually from SO) are
 * scanned to find foreground and background colors.
 */

static void
init_hilite(void)
{
    int c;
#ifdef TOS
    extern unsigned long tos_numcolors; /* in tos.c */
    static char NOCOL[] = "\033b0", COLHE[] = "\033q\033b0";

    if (tos_numcolors <= 2) {
        return;
    }
    /* Under TOS, the "bright" and "dim" colors are reversed. Moreover,
     * on the Falcon the dim colors are *really* dim; so we make most
     * of the colors the bright versions, with a few exceptions where
     * the dim ones look OK.
     */
    hilites[0] = NOCOL;
    for (c = 1; c < SIZE(hilites); c++) {
        char *foo;

        foo = (char *) alloc(sizeof "\033b0");
        if (tos_numcolors > 4)
            Sprintf(foo, "\033b%c", (c & ~BRIGHT) + '0');
        else
            Strcpy(foo, "\033b0");
        hilites[c] = foo;
    }

    if (tos_numcolors == 4) {
        TI = nhStr("\033b0\033c3\033E\033e");
        TE = nhStr("\033b3\033c0\033J");
        nh_HE = COLHE;
        hilites[CLR_GREEN] = hilites[CLR_GREEN | BRIGHT] = "\033b2";
        hilites[CLR_RED] = hilites[CLR_RED | BRIGHT] = "\033b1";
    } else {
        Sprintf(hilites[CLR_BROWN], "\033b%c", (CLR_BROWN ^ BRIGHT) + '0');
        Sprintf(hilites[CLR_GREEN], "\033b%c", (CLR_GREEN ^ BRIGHT) + '0');

        TI = nhStr("\033b0\033c\017\033E\033e");
        TE = nhStr("\033b\017\033c0\033J");
        nh_HE = COLHE;
        hilites[CLR_WHITE] = hilites[CLR_BLACK] = NOCOL;
        hilites[NO_COLOR] = hilites[CLR_GRAY];
    }

#else /* TOS */

    int backg, foreg, hi_backg, hi_foreg;

    for (c = 0; c < SIZE(hilites); c++)
        hilites[c] = nh_HI;
    hilites[CLR_GRAY] = hilites[NO_COLOR] = (char *) 0;

    analyze_seq(nh_HI, &hi_foreg, &hi_backg);
    analyze_seq(nh_HE, &foreg, &backg);

    for (c = 0; c < SIZE(hilites); c++)
        /* avoid invisibility */
        if ((backg & ~BRIGHT) != c) {
#ifdef MICRO
            if (c == CLR_BLUE)
                continue;
#endif
            if (c == foreg) {
                hilites[c] = (char *) 0;
            } else if (c != hi_foreg || backg != hi_backg) {
                hilites[c] = (char *) alloc(sizeof "\033[%d;3%d;4%dm");
                Sprintf(hilites[c], "\033[%d", !!(c & BRIGHT));
                if ((c | BRIGHT) != (foreg | BRIGHT))
                    Sprintf(eos(hilites[c]), ";3%d", c & ~BRIGHT);
                if (backg != CLR_BLACK)
                    Sprintf(eos(hilites[c]), ";4%d", backg & ~BRIGHT);
                Strcat(hilites[c], "m");
            }
        }

#ifdef MICRO
    /* brighten low-visibility colors */
    hilites[CLR_BLUE] = hilites[CLR_BLUE | BRIGHT];
#endif
#endif /* TOS */
}

static void
kill_hilite(void)
{
#ifndef TOS
    int c;

    for (c = 0; c < CLR_MAX / 2; c++) {
        if (hilites[c | BRIGHT] == hilites[c])
            hilites[c | BRIGHT] = 0;
        if (hilites[c] && hilites[c] != nh_HI)
            free((genericptr_t) hilites[c]), hilites[c] = 0;
        if (hilites[c | BRIGHT] && hilites[c | BRIGHT] != nh_HI)
            free((genericptr_t) hilites[c | BRIGHT]), hilites[c | BRIGHT] = 0;
    }
#endif
    return;
}
#endif /* UNIX && TERMINFO */
#endif /* TERMLIB */

#if !defined(TERMLIB) && defined(ANSI_DEFAULT)
static char adef_nilstring[] = "";

static void
init_hilite(void)
{
    int c;

    if (!hilites[CLR_BLACK])
        hilites[CLR_BLACK] = adef_nilstring;
    if (!hilites[CLR_BLACK | BRIGHT])
        hilites[CLR_BLACK | BRIGHT] = hilites[CLR_BLACK];

    if (!hilites[CLR_GRAY])
        hilites[CLR_GRAY] = adef_nilstring;
    if (!hilites[NO_COLOR])
        hilites[NO_COLOR] = hilites[CLR_GRAY];

    for (c = 0; c < CLR_MAX / 2; c++) {
        if (c == CLR_BLACK)
            continue;
        hilites[c | BRIGHT] = (char *) alloc(sizeof "\033[1;3%dm");
        Sprintf(hilites[c | BRIGHT], "\033[1;3%dm", c);
        if (c == CLR_GRAY)
            continue;
#ifdef MICRO
        if (c == CLR_BLUE) {
            hilites[CLR_BLUE] = hilites[CLR_BLUE | BRIGHT];
        } else
#endif
        {
            hilites[c] = (char *) alloc(sizeof "\033[0;3%dm");
            Sprintf(hilites[c], "\033[0;3%dm", c);
        }
    }

    /* See TERMLIB && UNIX && TERMINFO code above. */
    if (iflags.wc2_darkgray) {
        /* Bright black is dark gray. */
        hilites[CLR_BLACK] = (char *) alloc(sizeof "\033[1;30m");
        Sprintf(hilites[CLR_BLACK], "\033[1;30m");
    } else {
        /* Use blue for black. */
        hilites[CLR_BLACK] = hilites[CLR_BLUE];
    }
}

static void
kill_hilite(void)
{
    int c;

    for (c = 0; c < CLR_MAX / 2; c++) {
        if (c == CLR_GRAY || hilites[c] == adef_nilstring)
            hilites[c] = 0;
        if (hilites[c | BRIGHT] == adef_nilstring)
            hilites[c] = 0;
        if (c == CLR_BLACK)
            continue;
        if (hilites[c | BRIGHT] == hilites[c]) /* for blue */
            hilites[c | BRIGHT] = 0;
        if (hilites[c] && hilites[c] != nh_HI)
            free((genericptr_t) hilites[c]), hilites[c] = 0;
        if (hilites[c | BRIGHT] && hilites[c | BRIGHT] != nh_HI)
            free((genericptr_t) hilites[c | BRIGHT]), hilites[c | BRIGHT] = 0;
    }

    if (hilites[CLR_BLACK]) {
        if (hilites[CLR_BLACK] != hilites[CLR_BLUE])
            free(hilites[CLR_BLACK]);
        hilites[CLR_BLACK] = 0;
    }
}
#endif /* !TERMLIB && ANSI_DEFAULT */

static char nulstr[] = "";

static char *
s_atr2str(int n)
{
    switch (n) {
    case ATR_ITALIC:
        /* if italic isn't available, fall through to underline */
        if (ZH && *ZH)
            return ZH;
        FALLTHROUGH;
        /*FALLTHRU*/
    case ATR_BLINK:
    case ATR_ULINE:
        if (n == ATR_BLINK) {
            if (MB && *MB)
                return MB;
        } else { /* Underline */
            if (nh_US && *nh_US)
                return nh_US;
        }
        FALLTHROUGH;
        /*FALLTHRU*/
    case ATR_BOLD:
        if (MD && *MD)
            return MD;
        if (nh_HI && *nh_HI)
            return nh_HI;
        break;
    case ATR_INVERSE:
        if (MR && *MR)
            return MR;
        break;
    case ATR_DIM:
        if (MH && *MH)
            return MH;
        break;
    }
    return nulstr;
}

static char *
e_atr2str(int n)
{
    switch (n) {
    case ATR_ITALIC:
        /* send ZR unless we didn't have ZH and substituted US */
        if (ZR && *ZR && ZH && *ZH)
            return ZR;
        FALLTHROUGH;
        /*FALLTHRU*/
    case ATR_ULINE:
        if (nh_UE && *nh_UE)
            return nh_UE;
        FALLTHROUGH;
        /*FALLTHRU*/
    case ATR_BOLD:
    case ATR_BLINK:
        if (nh_HE && *nh_HE)
            return nh_HE;
        FALLTHROUGH;
        /*FALLTHRU*/
    case ATR_DIM:
    case ATR_INVERSE:
        if (ME && *ME)
            return ME;
        break;
    }
    return nulstr;
}

/* suppress nonfunctional highlights so render_status() might be able to
   optimize more; keep this in sync with s_atr2str() */
int
term_attr_fixup(int msk)
{
    /* underline is converted to bold if its start sequence isn't available */
    if ((msk & HL_ULINE) && (!nh_US || !*nh_US)) {
        msk |= HL_BOLD;
        msk &= ~HL_ULINE;
    }
    /* blink used to be converted to bold unconditionally; now depends on MB */
    if ((msk & HL_BLINK) && (!MB || !*MB)) {
        msk |= HL_BOLD;
        msk &= ~HL_BLINK;
    }
    /* dim is ignored if its start sequence isn't available */
    if ((msk & HL_DIM) && (!MH || !*MH)) {
        msk &= ~HL_DIM;
    }
    return msk;
}

void
term_start_attr(int attr)
{
    if (attr) {
#ifdef NOMUX_CAPTURE
        nomux_set_attr(attr);
#endif
        const char *astr = s_atr2str(attr);

        if (astr && *astr)
            xputs(astr);
    }
}

void
term_end_attr(int attr)
{
    if (attr) {
#ifdef NOMUX_CAPTURE
        nomux_end_attr();
#endif
        const char *astr = e_atr2str(attr);

        if (astr && *astr)
            xputs(astr);
    }
}

/* this is called 'start bold' but HI is derived from SO (standout) rather
   than from MD (start bold attribute) */
void
term_start_raw_bold(void)
{
    const char *soOn = nh_HI ? nh_HI : tty_standout_on;

    if (*soOn)
        xputs(soOn);
}

/* this is called 'end bold' but HE is derived from ME (end all attributes) */
void
term_end_raw_bold(void)
{
    const char *soOff = nh_HE ? nh_HE : tty_standout_off;

    if (*soOff)
        xputs(soOff);
}

void
term_end_color(void)
{
    xputs(nh_HE);
#ifdef NOMUX_CAPTURE
    nomux_end_fg();
    nomux_end_attr();
#endif
}

void
term_start_color(int color)
{
#ifdef NOMUX_CAPTURE
    nomux_set_fg(color);
#endif
    if (color == NO_COLOR)
        xputs(nh_HE); /* inline term_end_color() */
    else if (color < CLR_MAX && hilites[color] && *hilites[color])
        xputs(hilites[color]);
}

void
term_start_bgcolor(int color)
{
    char tmp[8];
    Sprintf(tmp, "\033[%dm", ((color % 8) + 40));
    xputs(tmp);
}

/* hide or show cursor */
void
term_curs_set(int visibility)
{
    static int vis = -1;

    if (vis == visibility)
        return;

    if (!visibility && nh_VI)
        xputs(nh_VI);
    else if (visibility && nh_VE)
        xputs(nh_VE);
    vis = visibility;
}

#ifdef CHANGE_COLOR
void
tty_change_color(int color, long rgb, int reverse UNUSED)
{
    char buf[BUFSZ];
    int i;
    char *c, *fmt;

    /* FIXME: colors are not reset back when exiting NetHack */
    if (nh_Ic && *nh_Ic) {
        long clr = color, r, g, b;

        /* color * 3 seems to work correctly? */
        /* this probably depends on the termcap definition */
        r = ((rgb >> 16) & 0xFF) * 3;
        g = ((rgb >> 8) & 0xFF) * 3;
        b = (rgb & 0xFF) * 3;


        fmt = tparm(nh_Ic, clr, r, g, b);

        c = fmt;

        i = 0;
        while (*c) {
            if (*c == '\033') {
                buf[i++] = '\\';
                buf[i++] = 'E';
            } else {
                buf[i++] = *c;
            }
            c++;
        }
        buf[i++] = '\0';
        xputs(fmt);
    }
}
#endif /* CHANGE_COLOR */


#ifndef SEP2
#define tcfmtstr "\033[38;2;%ld;%ld;%ldm"
#define tcfmtstr256 "\033[38;5;%dm"
#endif

static void emit24bit(long mcolor);
static void emit256(int u256coloridx);

static void emit24bit(long mcolor)
{
    static char tcolorbuf[QBUFSZ];

    Snprintf(tcolorbuf, sizeof tcolorbuf, tcfmtstr,
             ((mcolor >> 16) & 0xFF),   /* red */
             ((mcolor >>  8) & 0xFF),   /* green */
             ((mcolor >>  0) & 0xFF));  /* blue */
    xputs(tcolorbuf);
}

static void emit256(int color256idx)
{
    static char tcolorbuf[QBUFSZ];

    Snprintf(tcolorbuf, sizeof tcolorbuf, tcfmtstr256,
             color256idx);
    xputs(tcolorbuf);
}

void
term_start_extracolor(uint32 customcolor, uint16 color256idx)
{
    /* color 0 has bit 0x1000000 set */
    long mcolor = (customcolor & 0xFFFFFF);
#ifdef NOMUX_CAPTURE
    /* Record before emitting: these writers bypass nomux_putch by design. */
    nomux_set_extracolor((unsigned int) color256idx);
#endif
    if (iflags.colorcount == 256)
        emit256(color256idx);
    else
        emit24bit(mcolor);
}

void
term_end_extracolor(void)
{
#ifdef NOMUX_CAPTURE
        nomux_end_extracolor();
#endif
        xputs("\033[0m");
}
#endif /* TTY_GRAPHICS && !NO_TERMS  */

/*termcap.c*/
