/* $NHDT-Header: NetHack 5.0 rnd.c 2026/06/20_22:26:43 NetHack-5.0:1.43 $ */
/*      Copyright (c) 2004 by Robert Patrick Rankin               */
/* NetHack may be freely redistributed.  See license for details. */

/* Suppress rng logging macros inside this file -- we define the real
   functions here, and add logging directly in each function body.
   Internal calls (e.g. rnz calling rn2) inherit the outer caller's
   context because the macros don't fire inside rnd.c. */
#define RNGLOG_IN_RND_C
#include "hack.h"

/*
 * PRNG call logging infrastructure.
 * When NETHACK_RNGLOG is set, every rn2/rnd/rnl/d/rne/rnz call is logged
 * with: call#, function(args) = result @ caller_func(file:line)
 *
 * The caller_func, file:line come from rng_log_set_caller(), invoked
 * by the macros in hack.h before calling the real function.
 *
 * Caller context propagation: the context is NOT cleared after logging.
 * This means internal rn2 calls from wrapper functions (rnz, rne, rnl)
 * inherit the ORIGINAL caller's file:line annotation.  For example,
 * if start_corpse_timeout at mkobj.c:1409 calls rnz(25), the internal
 * rn2(1000), rn2(4), rn2(2) calls all log with "@ mkobj.c:1409".
 * The wrapper result (rnz, rne, rnl) also logs with the same annotation.
 *
 * Context is overwritten by the next macro expansion (each external call
 * sets fresh context), so there is no stale-context problem.
 */
static FILE *rng_logfile = NULL;
static int rng_call_count = 0;
static const char *rng_caller_file = NULL;
static int rng_caller_line = 0;
static const char *rng_caller_func = NULL;
/* NETHACK_TRACE=<channel>[,<channel>...]: the debug channels that write
   ^dbg.<channel> lines into the log (#1724).  PES ignores every ^dbg. line,
   so turning a channel on never changes parity. */
static const char *trace_mask = (const char *) 0;
static const char *repaint_debug_scope = (const char *) 0;
#define COSMIC_OWNER_STACK_SIZE 32
static const char *cosmic_owner_stack[COSMIC_OWNER_STACK_SIZE];
static int cosmic_owner_depth = 0;
static coordxy cosmic_cell_x = 0;
static coordxy cosmic_cell_y = 0;
static boolean cosmic_cell_active = FALSE;
static const char *cosmic_newsym_branch = (const char *) 0;
static const char *cosmic_maploc_branch = (const char *) 0;
static const char *cosmic_rng_kind = (const char *) 0;
staticfn const char *cosmic_display_current_owner(void);

void
rng_log_init(void)
{
    const char *logpath = harness_getenv("NETHACK_RNGLOG");

    if (logpath) {
        rng_logfile = fopen(logpath, "w");
        if (rng_logfile)
            setvbuf(rng_logfile, NULL, _IOLBF, 0); /* line-buffered */
    }
    trace_mask = harness_getenv("NETHACK_TRACE");
}

/* Is debug channel `channel` listed in NETHACK_TRACE?  Channels are
   separated by commas.  Nothing is on without a log to write to. */
boolean
harness_trace_on(const char *channel)
{
    const char *p;
    size_t len;

    if (!trace_mask || !rng_logfile)
        return FALSE;
    len = strlen(channel);
    for (p = trace_mask; (p = strstr(p, channel)) != 0; p += len) {
        if ((p == trace_mask || p[-1] == ',')
            && (p[len] == '\0' || p[len] == ','))
            return TRUE;
    }
    return FALSE;
}

/* Write one debug line, ^dbg.<channel><fmt>, when the channel is on.
   Callers start fmt with "[" for a plain line or ".<event>[" for one of
   several event kinds on a channel. */
void
dbg_log(const char *channel, const char *fmt, ...)
{
    va_list ap;

    if (!harness_trace_on(channel))
        return;
    fprintf(rng_logfile, "^dbg.%s", channel);
    va_start(ap, fmt);
    vfprintf(rng_logfile, fmt, ap);
    va_end(ap);
    fputc('\n', rng_logfile);
}

boolean
rng_log_enabled(void)
{
    return rng_logfile ? TRUE : FALSE;
}

void
rng_log_set_caller(const char *file, int line, const char *func)
{
    rng_caller_file = file;
    rng_caller_line = line;
    rng_caller_func = func;
}

long
rng_log_get_position(void)
{
    if (!rng_logfile)
        return -1L;
    (void) fflush(rng_logfile);
    return ftell(rng_logfile);
}

int
rng_log_get_call_count(void)
{
    return rng_call_count;
}

#define MIDLOG_STACK_SIZE 16
static int midlog_stack[MIDLOG_STACK_SIZE];
static int midlog_depth = 0;

void
midlog_enter(const char *fn, const char *file, int line, const char *caller)
{
    if (!rng_logfile)
        return;
    if (midlog_depth < MIDLOG_STACK_SIZE)
        midlog_stack[midlog_depth] = rng_call_count;
    midlog_depth++;
    fprintf(rng_logfile, ">%s @ %s(%s:%d)\n", fn, caller, file, line);
}

void
midlog_exit_int(const char *fn, int result,
                const char *file, int line, const char *caller)
{
    int entry = 0;

    if (!rng_logfile)
        return;
    --midlog_depth;
    if (midlog_depth >= 0 && midlog_depth < MIDLOG_STACK_SIZE)
        entry = midlog_stack[midlog_depth];
    fprintf(rng_logfile, "<%s=%d #%d-%d @ %s(%s:%d)\n",
            fn, result, entry + 1, rng_call_count, caller, file, line);
}

void
midlog_exit_void(const char *fn,
                 const char *file, int line, const char *caller)
{
    int entry = 0;

    if (!rng_logfile)
        return;
    --midlog_depth;
    if (midlog_depth >= 0 && midlog_depth < MIDLOG_STACK_SIZE)
        entry = midlog_stack[midlog_depth];
    fprintf(rng_logfile, "<%s #%d-%d @ %s(%s:%d)\n",
            fn, entry + 1, rng_call_count, caller, file, line);
}

void
midlog_exit_ptr(const char *fn, const void *result,
                const char *file, int line, const char *caller)
{
    int entry = 0;

    (void) result;
    if (!rng_logfile)
        return;
    --midlog_depth;
    if (midlog_depth >= 0 && midlog_depth < MIDLOG_STACK_SIZE)
        entry = midlog_stack[midlog_depth];
    fprintf(rng_logfile, "<%s #%d-%d @ %s(%s:%d)\n",
            fn, entry + 1, rng_call_count, caller, file, line);
}

boolean
cosmic_display_log_enabled(void)
{
    return harness_trace_on("display");
}

staticfn const char *
cosmic_display_current_owner(void)
{
    if (cosmic_owner_depth <= 0)
        return (const char *) 0;
    return cosmic_owner_stack[cosmic_owner_depth - 1];
}

void
cosmic_display_push_owner(const char *owner)
{
    const char *parent;

    if (!cosmic_display_log_enabled())
        return;
    parent = cosmic_display_current_owner();
    if (cosmic_owner_depth < COSMIC_OWNER_STACK_SIZE)
        cosmic_owner_stack[cosmic_owner_depth] = owner;
    cosmic_owner_depth++;
    dbg_log("display", ".owner_begin[owner=%s depth=%d%s%s]",
            owner ? owner : "?", cosmic_owner_depth,
            parent ? " parent=" : "",
            parent ? parent : "");
}

void
cosmic_display_pop_owner(const char *owner)
{
    const char *current;

    if (!cosmic_display_log_enabled())
        return;
    current = cosmic_display_current_owner();
    dbg_log("display", ".owner_end[owner=%s depth=%d%s%s]",
            owner ? owner : "?", cosmic_owner_depth,
            current ? " current=" : "",
            current ? current : "");
    if (cosmic_owner_depth > 0)
        --cosmic_owner_depth;
}

void
cosmic_display_set_cell(coordxy x, coordxy y)
{
    cosmic_cell_x = x;
    cosmic_cell_y = y;
    cosmic_cell_active = TRUE;
}

void
cosmic_display_clear_cell(void)
{
    cosmic_cell_x = 0;
    cosmic_cell_y = 0;
    cosmic_cell_active = FALSE;
}

void
cosmic_display_set_newsym_branch(const char *branch)
{
    cosmic_newsym_branch = branch;
}

void
cosmic_display_clear_newsym_branch(void)
{
    cosmic_newsym_branch = (const char *) 0;
}

void
cosmic_display_set_maploc_branch(const char *branch)
{
    cosmic_maploc_branch = branch;
}

void
cosmic_display_clear_maploc_branch(void)
{
    cosmic_maploc_branch = (const char *) 0;
}

void
cosmic_display_prepare_rng_kind(const char *kind)
{
    cosmic_rng_kind = kind;
}

void
cosmic_display_log_newsym(coordxy x, coordxy y, const char *branch, int cansee)
{
    if (!cosmic_display_log_enabled())
        return;
    cosmic_display_set_cell(x, y);
    cosmic_display_set_newsym_branch(branch);
    dbg_log("display", ".newsym[owner=%s x=%d y=%d cansee=%d branch=%s]",
            cosmic_display_current_owner()
                ? cosmic_display_current_owner()
                : "?",
            x, y, cansee, branch ? branch : "?");
}

void
cosmic_display_log_maploc(coordxy x, coordxy y, const char *branch, int show)
{
    if (!cosmic_display_log_enabled())
        return;
    cosmic_display_set_cell(x, y);
    cosmic_display_set_maploc_branch(branch);
    dbg_log("display", ".maploc[owner=%s x=%d y=%d show=%d branch=%s]",
            cosmic_display_current_owner()
                ? cosmic_display_current_owner()
                : "?",
            x, y, show, branch ? branch : "?");
}

void
cosmic_display_log_tty_menu_show(
    int window, const char *type, int offx, int offy,
    long maxrow, long maxcol, int overlay, int fullscreen, int blocking)
{
    if (!cosmic_display_log_enabled())
        return;
    dbg_log("display",
        ".tty_menu_show[owner=%s win=%d type=%s offx=%d offy=%d maxrow=%ld maxcol=%ld overlay=%d fullscreen=%d blocking=%d]",
        cosmic_display_current_owner() ? cosmic_display_current_owner() : "?",
        window, type ? type : "?", offx, offy, maxrow, maxcol, overlay,
        fullscreen, blocking);
}

void
cosmic_display_log_tty_menu_draw(
    int window, const char *type, int row0, int rowN,
    long page_lines, int npages, int curr_page, const char *morestr)
{
    if (!cosmic_display_log_enabled())
        return;
    dbg_log("display",
        ".tty_menu_draw[owner=%s win=%d type=%s row0=%d rowN=%d page_lines=%ld page=%d npages=%d%s%s]",
        cosmic_display_current_owner() ? cosmic_display_current_owner() : "?",
        window, type ? type : "?", row0, rowN, page_lines, curr_page,
        npages, morestr ? " more=" : "", morestr ? morestr : "");
}

void
cosmic_display_log_tty_menu_hide(int window, const char *type, int active,
                                 int clearscreen)
{
    if (!cosmic_display_log_enabled())
        return;
    dbg_log("display",
        ".tty_menu_hide[owner=%s win=%d type=%s active=%d clearscreen=%d]",
        cosmic_display_current_owner() ? cosmic_display_current_owner() : "?",
        window, type ? type : "?", active, clearscreen);
}

void
cosmic_display_log_tty_menu_erase(
    int window, const char *type, int x0, int y0, int x1, int y1,
    int clear, int restore)
{
    if (!cosmic_display_log_enabled())
        return;
    dbg_log("display",
        ".tty_menu_erase[owner=%s win=%d type=%s x0=%d y0=%d x1=%d y1=%d clear=%d restore=%d]",
        cosmic_display_current_owner() ? cosmic_display_current_owner() : "?",
        window, type ? type : "?", x0, y0, x1, y1, clear, restore);
}

static void
rng_log_write(const char *func, const char *args, int result)
{
    if (!rng_logfile)
        return;
    rng_call_count++;
    if (rng_caller_file) {
        if (rng_caller_func) {
            fprintf(rng_logfile, "%d %s(%s) = %d @ %s(%s:%d)\n",
                    rng_call_count, func, args, result,
                    rng_caller_func, rng_caller_file, rng_caller_line);
        } else {
            fprintf(rng_logfile, "%d %s(%s) = %d @ %s:%d\n",
                    rng_call_count, func, args, result,
                    rng_caller_file, rng_caller_line);
        }
        /* Do NOT clear rng_caller_file here -- let internal calls from
           wrapper functions (rnz, rne, rnl) inherit the same context. */
    } else {
        fprintf(rng_logfile, "%d %s(%s) = %d\n",
                rng_call_count, func, args, result);
    }
}

/*
 * Stable monster event reference formatting.
 * Include m_id when a monster pointer is available to disambiguate mons.
 */
const char *
event_mon_ref(const struct monst *mon, int mnum, coordxy x, coordxy y)
{
    static char refbuf[64];
    if (mon) {
        Snprintf(refbuf, sizeof refbuf, "%d#%lu@%d,%d", mnum,
                 (unsigned long) mon->m_id, (int) x, (int) y);
    } else {
        Snprintf(refbuf, sizeof refbuf, "%d@%d,%d", mnum, (int) x, (int) y);
    }

    return refbuf;
}

/*
 * Event logging.
 *
 * Writes ^event lines interleaved with the RNG log so differential-debug
 * consumers can correlate state-changing events with the PRNG stream.
 */
void
event_log(const char *fmt, ...)
{
    va_list ap;

    if (!rng_logfile)
        return;
    fputc('^', rng_logfile);
    va_start(ap, fmt);
    vfprintf(rng_logfile, fmt, ap);
    va_end(ap);
    fputc('\n', rng_logfile);
}

/* ^dbg.repaint[...]: the flush/bot/more/yn/mark repaint trace. */
void
repaint_log(const char *fmt, ...)
{
    va_list ap;

    if (!harness_trace_on("repaint"))
        return;
    fputs("^dbg.repaint[", rng_logfile);
    va_start(ap, fmt);
    vfprintf(rng_logfile, fmt, ap);
    va_end(ap);
    fputs("]\n", rng_logfile);
}

/* ^dbg.repaint_debug[...]: the owner-tagged repaint trace, prefixed with
   the step, command path and key being processed. */
void
repaint_debug_log(const char *fmt, ...)
{
    va_list ap;
    long step_index;
    const char *step_path;
    int step_key;

    if (!harness_trace_on("repaint_debug"))
        return;
    fputs("^dbg.repaint_debug[", rng_logfile);
    step_index = repaint_debug_get_step_index();
    if (step_index >= 0L)
        fprintf(rng_logfile, "step=%ld ", step_index + 1L);
    step_path = repaint_debug_get_step_path();
    if (step_path && *step_path)
        fprintf(rng_logfile, "path=%s ", step_path);
    step_key = repaint_debug_get_step_key();
    if (step_key >= 0)
        fprintf(rng_logfile, "key=%d ", step_key);
    va_start(ap, fmt);
    vfprintf(rng_logfile, fmt, ap);
    va_end(ap);
    fputs("]\n", rng_logfile);
}

void
repaint_debug_set_scope(const char *scope)
{
    repaint_debug_scope = scope;
}

void
repaint_debug_clear_scope(void)
{
    repaint_debug_scope = (const char *) 0;
}

const char *
repaint_debug_get_scope(void)
{
    return repaint_debug_scope;
}

#ifdef USE_ISAAC64
#include "isaac64.h"

staticfn int whichrng(int (*fn)(int));
staticfn int RND(int);
staticfn void set_random(unsigned long, int (*)(int));

#if 0
static isaac64_ctx rng_state;
#endif

struct rnglist_t {
    int (*fn)(int);
    boolean init;
    isaac64_ctx rng_state;
};

enum { CORE = 0, DISP = 1 };

static struct rnglist_t rnglist[] = {
    { rn2, FALSE, { 0 } },                      /* CORE */
    { rn2_on_display_rng, FALSE, { 0 } },       /* DISP */
};

staticfn int
whichrng(int (*fn)(int))
{
    int i;

    for (i = 0; i < SIZE(rnglist); ++i)
        if (rnglist[i].fn == fn)
            return i;
    return -1;
}

void
init_isaac64(unsigned long seed, int (*fn)(int))
{
    unsigned char new_rng_state[sizeof seed];
    unsigned i;
    int rngindx = whichrng(fn);

    if (rngindx < 0)
        panic("Bad rng function passed to init_isaac64().");

    for (i = 0; i < sizeof seed; i++) {
        new_rng_state[i] = (unsigned char) (seed & 0xFF);
        seed >>= 8;
    }
    isaac64_init(&rnglist[rngindx].rng_state, new_rng_state,
                 (int) sizeof seed);
}

staticfn int
RND(int x)
{
    return (isaac64_next_uint64(&rnglist[CORE].rng_state) % x);
}

/* 0 <= rn2(x) < x, but on a different sequence from the "main" rn2;
   used in cases where the answer doesn't affect gameplay and we don't
   want to give users easy control over the main RNG sequence. */
int
rn2_on_display_rng(int x)
{
    int result = (isaac64_next_uint64(&rnglist[DISP].rng_state) % x);

    /* ^dbg.disp_rng[n=N idx=R ...] @ caller.  A display-RNG call is not
       a core RNG call, so it does not advance rng_call_count.  With the
       display channel on as well, the line names the owner, the kind of
       draw, the cell and the newsym/maploc branch. */
    if (harness_trace_on("disp_rng")) {
        fprintf(rng_logfile, "^dbg.disp_rng[n=%d idx=%d", x, result);
        if (cosmic_display_log_enabled())
            fprintf(rng_logfile, " owner=%s kind=%s x=%d y=%d%s%s%s%s",
                    cosmic_display_current_owner()
                        ? cosmic_display_current_owner()
                        : "?",
                    cosmic_rng_kind ? cosmic_rng_kind : "?",
                    cosmic_cell_active ? cosmic_cell_x : -1,
                    cosmic_cell_active ? cosmic_cell_y : -1,
                    cosmic_newsym_branch ? " newsym=" : "",
                    cosmic_newsym_branch ? cosmic_newsym_branch : "",
                    cosmic_maploc_branch ? " maploc=" : "",
                    cosmic_maploc_branch ? cosmic_maploc_branch : "");
        fputc(']', rng_logfile);
        if (rng_caller_file) {
            if (rng_caller_func)
                fprintf(rng_logfile, " @ %s(%s:%d)",
                        rng_caller_func, rng_caller_file, rng_caller_line);
            else
                fprintf(rng_logfile, " @ %s:%d",
                        rng_caller_file, rng_caller_line);
        }
        fputc('\n', rng_logfile);
    }
    cosmic_rng_kind = (const char *) 0;
    return result;
}

#else   /* USE_ISAAC64 */

/* "Rand()"s definition is determined by [OS]conf.h */
#if defined(UNIX) || defined(RANDOM)
#define RND(x) ((int) (Rand() % (long) (x)))
#else
/* Good luck: the bottom order bits are cyclic. */
#define RND(x) ((int) ((Rand() >> 3) % (x)))
#endif
int
rn2_on_display_rng(int x)
{
    static unsigned seed = 1;
    seed *= 2739110765;
    return (int) ((seed >> 16) % (unsigned) x);
}
#endif  /* USE_ISAAC64 */

/* 0 <= rn2(x) < x */
int
rn2(int x)
{
    int result;
#if (NH_DEVEL_STATUS != NH_STATUS_RELEASED)
    if (x <= 0) {
        impossible("rn2(%d) attempted", x);
        return 0;
    }
#endif
    result = RND(x);
    if (rng_logfile) {
        char buf[32];
        snprintf(buf, sizeof buf, "%d", x);
        rng_log_write("rn2", buf, result);
    }
    return result;
}

/* 0 <= rnl(x) < x; sometimes subtracting Luck;
   good luck approaches 0, bad luck approaches (x-1) */
int
rnl(int x)
{
    int i, adjustment;
    /* Internal rn2 call inherits caller context from the rnl macro --
       no save/clear needed. */

#if (NH_DEVEL_STATUS != NH_STATUS_RELEASED)
    if (x <= 0) {
        impossible("rnl(%d) attempted", x);
        return 0;
    }
#endif

    adjustment = Luck;
    if (x <= 15) {
        /* for small ranges, use Luck/3 (rounded away from 0);
           also guard against architecture-specific differences
           of integer division involving negative values */
        adjustment = (abs(adjustment) + 1) / 3 * sgn(adjustment);
        /*
         *       11..13 ->  4
         *        8..10 ->  3
         *        5.. 7 ->  2
         *        2.. 4 ->  1
         *       -1,0,1 ->  0 (no adjustment)
         *       -4..-2 -> -1
         *       -7..-5 -> -2
         *      -10..-8 -> -3
         *      -13..-11-> -4
         */
    }

    i = RND(x);
    if (adjustment && rn2(37 + abs(adjustment))) {
        i -= adjustment;
        if (i < 0)
            i = 0;
        else if (i >= x)
            i = x - 1;
    }
    if (rng_logfile) {
        char buf[32];
        snprintf(buf, sizeof buf, "%d", x);
        rng_log_write("rnl", buf, i);
    }
    return i;
}

/* 1 <= rnd(x) <= x */
int
rnd(int x)
{
    int result;
#if (NH_DEVEL_STATUS != NH_STATUS_RELEASED)
    if (x <= 0) {
        impossible("rnd(%d) attempted", x);
        return 1;
    }
#endif
    result = RND(x) + 1;
    if (rng_logfile) {
        char buf[32];
        snprintf(buf, sizeof buf, "%d", x);
        rng_log_write("rnd", buf, result);
    }
    return result;
}

int
rnd_on_display_rng(int x)
{
    return rn2_on_display_rng(x) + 1;
}

/* d(N,X) == NdX == dX+dX+...+dX N times; n <= d(n,x) <= (n*x) */
int
d(int n, int x)
{
    int tmp = n;
    int orig_n = n;

#if (NH_DEVEL_STATUS != NH_STATUS_RELEASED)
    if (x < 0 || n < 0 || (x == 0 && n != 0)) {
        impossible("d(%d,%d) attempted", n, x);
        return 1;
    }
#endif
    while (n--)
        tmp += RND(x);
    if (rng_logfile) {
        char buf[64];
        snprintf(buf, sizeof buf, "%d,%d", orig_n, x);
        rng_log_write("d", buf, tmp);
    }
    return tmp; /* Alea iacta est. -- J.C. */
}

/* 1 <= rne(x) <= max(u.ulevel/3,5) */
int
rne(int x)
{
    int tmp, utmp;
    /* Internal rn2 calls inherit caller context from the rne macro --
       no save/clear needed. */

    utmp = (u.ulevel < 15) ? 5 : u.ulevel / 3;
    tmp = 1;
    while (tmp < utmp && !rn2(x))
        tmp++;
    if (rng_logfile) {
        char buf[32];
        snprintf(buf, sizeof buf, "%d", x);
        rng_log_write("rne", buf, tmp);
    }
    return tmp;

    /* was:
     *  tmp = 1;
     *  while (!rn2(x))
     *    tmp++;
     *  return min(tmp, (u.ulevel < 15) ? 5 : u.ulevel / 3);
     * which is clearer but less efficient and stands a vanishingly
     * small chance of overflowing tmp
     */
}

/* rnz: everyone's favorite! */
int
rnz(int i)
{
    long x = (long) i;
    long tmp = 1000L;
    /* Internal rn2/rne calls inherit caller context from the rnz macro --
       no save/clear needed. */

    tmp += rn2(1000);
    tmp *= rne(4);
    if (rn2(2)) {
        x *= tmp;
        x /= 1000;
    } else {
        x *= 1000;
        x /= tmp;
    }
    if (rng_logfile) {
        char buf[32];
        snprintf(buf, sizeof buf, "%d", i);
        rng_log_write("rnz", buf, (int) x);
    }
    return (int) x;
}

/* Sets the seed for the random number generator */
#ifdef USE_ISAAC64

staticfn void
set_random(unsigned long seed,
           int (*fn)(int))
{
    init_isaac64(seed, fn);
}

#else /* USE_ISAAC64 */

/*ARGSUSED*/
staticfn void
set_random(unsigned long seed,
           int (*fn)(int) UNUSED)
{
    /*
     * The types are different enough here that sweeping the different
     * routine names into one via #defines is even more confusing.
     */
# ifdef RANDOM /* srandom() from sys/share/random.c */
    srandom((unsigned int) seed);
# else
#  if defined(__APPLE__) || defined(BSD) || defined(LINUX) \
    || defined(ULTRIX) || defined(CYGWIN32) /* system srandom() */
#   if defined(BSD) && !defined(POSIX_TYPES) && defined(SUNOS4)
    (void)
#   endif
        srandom((int) seed);
#  else
#   ifdef UNIX /* system srand48() */
    srand48((long) seed);
#   else       /* poor quality system routine */
    srand((int) seed);
#   endif
#  endif
# endif
}
#endif /* USE_ISAAC64 */

/* An appropriate version of this must always be provided in
   port-specific code somewhere. It returns a number suitable
   as seed for the random number generator */
extern unsigned long sys_random_seed(void);

/*
 * Initializes the random number generator.
 * Only call once.
 */
void
init_random(int (*fn)(int))
{
    set_random(sys_random_seed(), fn);
}

/* Reshuffles the random number generator. */
void
reseed_random(int (*fn)(int))
{
   /* only reseed if we are certain that the seed generation is unguessable
    * by the players. */
    if (has_strong_rngseed)
        init_random(fn);
}

/* randomize the given list of numbers  0 <= i < count */
void
shuffle_int_array(int *indices, int count)
{
    int i, iswap, temp;

    for (i = count - 1; i > 0; i--) {
        if ((iswap = rn2(i + 1)) == i)
            continue;
        temp = indices[i];
        indices[i] = indices[iswap];
        indices[iswap] = temp;
    }
}

/*rnd.c*/
