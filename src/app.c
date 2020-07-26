#include "app.h"
#include "ui/command.h"
#include "ui/window.h"
#include "ui/inputwidget.h"
#include "ui/labelwidget.h"
#include "ui/documentwidget.h"
#include "ui/util.h"
#include "ui/text.h"
#include "ui/color.h"

#include <the_Foundation/commandline.h>
#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/path.h>
#include <the_Foundation/process.h>
#include <the_Foundation/sortedarray.h>
#include <the_Foundation/time.h>
#include <SDL_events.h>
#include <SDL_render.h>
#include <SDL_video.h>

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

#if defined (iPlatformApple) && !defined (iPlatformIOS)
#   include "ui/macos.h"
#endif

iDeclareType(App)
iDeclareType(HistoryItem)

struct Impl_HistoryItem {
    iTime   when;
    iString url;
};

void init_HistoryItem(iHistoryItem *d) {
    initCurrent_Time(&d->when);
    init_String(&d->url);
}

void deinit_HistoryItem(iHistoryItem *d) {
    deinit_String(&d->url);
}

#if defined (iPlatformApple)
static const char *dataDir_App_ = "~/Library/Application Support/fi.skyjake.Lagrange";
#endif
#if defined (iPlatformMsys)
static const char *dataDir_App_ = "~/AppData/Roaming/fi.skyjake.Lagrange";
#endif
#if defined (iPlatformLinux)
static const char *dataDir_App_ = "~/.config/lagrange";
#endif
static const char *prefsFileName_App_   = "prefs.cfg";
static const char *historyFileName_App_ = "history.txt";

static const size_t historyMax_App_ = 100;

struct Impl_App {
    iCommandLine args;
    iBool        running;
    iWindow *    window;
    iSortedArray tickers;
    iBool        pendingRefresh;
    iArray       history;
    size_t       historyPos; /* zero at the latest item */
    /* Preferences: */
    iBool        retainWindowSize;
    float        uiScale;
};

static iApp app_;

iDeclareType(Ticker)

struct Impl_Ticker {
    iAny *context;
    void (*callback)(iAny *);
};

static int cmp_Ticker_(const void *a, const void *b) {
    const iTicker *elems[2] = { a, b };
    return iCmp(elems[0]->context, elems[1]->context);
}

const iString *dateStr_(const iDate *date) {
    return collectNewFormat_String("%d-%02d-%02d %02d:%02d:%02d",
                                   date->year,
                                   date->month,
                                   date->day,
                                   date->hour,
                                   date->minute,
                                   date->second);
}

static iString *serializePrefs_App_(const iApp *d) {
    iString *str = new_String();
    iWindow *win = get_Window();
    if (d->retainWindowSize) {
        int w, h, x, y;
        SDL_GetWindowSize(d->window->win, &w, &h);
        SDL_GetWindowPosition(d->window->win, &x, &y);
        appendFormat_String(str, "restorewindow width:%d height:%d coord:%d %d\n", w, h, x, y);
    }
    appendFormat_String(str, "uiscale arg:%f\n", uiScale_Window(d->window));
    return str;
}

static const iString *prefsFileName_(void) {
    return collect_String(concatCStr_Path(&iStringLiteral(dataDir_App_), prefsFileName_App_));
}

static const iString *historyFileName_(void) {
    return collect_String(concatCStr_Path(&iStringLiteral(dataDir_App_), historyFileName_App_));
}

static void loadPrefs_App_(iApp *d) {
    iUnused(d);
    /* Create the data dir if it doesn't exist yet. */
    makeDirs_Path(collectNewCStr_String(dataDir_App_));
    iFile *f = new_File(prefsFileName_());
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        iString *str = readString_File(f);
        const iRangecc src = range_String(str);
        iRangecc line = iNullRange;
        while (nextSplit_Rangecc(&src, "\n", &line)) {
            iString cmd;
            initRange_String(&cmd, line);
            if (equal_Command(cstr_String(&cmd), "uiscale")) {
                /* Must be handled before the window is created. */
                setUiScale_Window(get_Window(), argf_Command(cstr_String(&cmd)));
            }
            else {
                postCommandString_App(&cmd);
            }
            deinit_String(&cmd);
        }
        delete_String(str);
    }
    else {
        /* default preference values */
    }
    iRelease(f);
}

static void savePrefs_App_(const iApp *d) {
    iString *cfg = serializePrefs_App_(d);
    iFile *f = new_File(prefsFileName_());
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        write_File(f, &cfg->chars);
    }
    iRelease(f);
    delete_String(cfg);
}

static void saveHistory_App_(const iApp *d) {
    iFile *f = new_File(historyFileName_());
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        iString *line = new_String();
        iConstForEach(Array, i, &d->history) {
            const iHistoryItem *item = i.value;
            iDate date;
            init_Date(&date, &item->when);
            format_String(line,
                          "%04d-%02d-%02dT%02d:%02d:%02d %s\n",
                          date.year,
                          date.month,
                          date.day,
                          date.hour,
                          date.minute,
                          date.second,
                          cstr_String(&item->url));
            writeData_File(f, cstr_String(line), size_String(line));
        }
        delete_String(line);
    }
    iRelease(f);
}

static void loadHistory_App_(iApp *d) {
    iFile *f = new_File(historyFileName_());
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        iString *src = newBlock_String(collect_Block(readAll_File(f)));
        const iRangecc range = range_String(src);
        iRangecc line = iNullRange;
        while (nextSplit_Rangecc(&range, "\n", &line)) {
            int y, m, D, H, M, S;
            sscanf(line.start, "%04d-%02d-%02dT%02d:%02d:%02d", &y, &m, &D, &H, &M, &S);
            if (!y) break;
            iHistoryItem item;
            init_HistoryItem(&item);
            init_Time(
                &item.when,
                &(iDate){ .year = y, .month = m, .day = D, .hour = H, .minute = M, .second = S });
            initCStrN_String(&item.url, line.start + 20, size_Range(&line) - 20);
            pushBack_Array(&d->history, &item);
        }
        delete_String(src);
    }
    iRelease(f);
}

static void clearHistory_App_(iApp *d) {
    iForEach(Array, i, &d->history) {
        deinit_HistoryItem(i.value);
    }
    clear_Array(&d->history);
}

static void init_App_(iApp *d, int argc, char **argv) {
    init_CommandLine(&d->args, argc, argv);
    init_SortedArray(&d->tickers, sizeof(iTicker), cmp_Ticker_);
    init_Array(&d->history, sizeof(iHistoryItem));
    d->historyPos       = 0;
    d->running          = iFalse;
    d->window           = NULL;
    d->retainWindowSize = iTrue;
    d->pendingRefresh   = iFalse;
    loadPrefs_App_(d);
    loadHistory_App_(d);
    d->window = new_Window();
    /* Widget state init. */ {
        postCommand_App("navigate.home");
    }
}

static void deinit_App(iApp *d) {
    savePrefs_App_(d);
    saveHistory_App_(d);
    clearHistory_App_(d);
    deinit_Array(&d->history);
    deinit_SortedArray(&d->tickers);
    delete_Window(d->window);
    d->window = NULL;
    deinit_CommandLine(&d->args);    
}

const iString *execPath_App(void) {
    return executablePath_CommandLine(&app_.args);
}

void processEvents_App(enum iAppEventMode eventMode) {
    iApp *d = &app_;
    SDL_Event ev;
    while (
        (!d->pendingRefresh && eventMode == waitForNewEvents_AppEventMode && SDL_WaitEvent(&ev)) ||
        ((d->pendingRefresh || eventMode == postedEventsOnly_AppEventMode) && SDL_PollEvent(&ev))) {
        switch (ev.type) {
            case SDL_QUIT:
                // if (isModified_Song(d->song)) {
                //     save_App_(d, autosavePath_App_(d));
                // }
                d->running = iFalse;
                goto backToMainLoop;
            case SDL_DROPFILE:
                postCommandf_App("open url:file://%s", ev.drop.file);
                break;
            default: {
                if (ev.type == SDL_USEREVENT && ev.user.code == refresh_UserEventCode) {
                    goto backToMainLoop;
                }
                iBool wasUsed = processEvent_Window(d->window, &ev);
                if (ev.type == SDL_USEREVENT && ev.user.code == command_UserEventCode) {
#if defined (iPlatformApple) && !defined (iPlatformIOS)
                    handleCommand_MacOS(command_UserEvent(&ev));
#endif
                    if (isCommand_UserEvent(&ev, "metrics.changed")) {
                        arrange_Widget(d->window->root);
                    }
                    if (!wasUsed) {
                        /* No widget handled the command, so we'll do it. */
                        handleCommand_App(ev.user.data1);
                    }
                    /* Allocated by postCommand_Apps(). */
                    free(ev.user.data1);
                }
                break;
            }
        }
    }
backToMainLoop:;
}

static void runTickers_App_(iApp *d) {
    /* Tickers may add themselves again, so we'll run off a copy. */
    iSortedArray *pending = copy_SortedArray(&d->tickers);
    clear_SortedArray(&d->tickers);
    if (!isEmpty_SortedArray(pending)) {
        postRefresh_App();
    }
    iConstForEach(Array, i, &pending->values) {
        const iTicker *ticker = i.value;
        if (ticker->callback) {
            ticker->callback(ticker->context);
        }
    }
    delete_SortedArray(pending);
}

static int run_App_(iApp *d) {
    arrange_Widget(findWidget_App("root"));
    d->running = iTrue;
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE); /* open files via drag'n'drop */
    while (d->running) {
        runTickers_App_(d);
        processEvents_App(waitForNewEvents_AppEventMode);
        refresh_App();
    }
    return 0;
}

void refresh_App(void) {
    iApp *d = &app_;
    destroyPending_Widget();
    draw_Window(d->window);
    recycle_Garbage();
    d->pendingRefresh = iFalse;
}

int run_App(int argc, char **argv) {
    init_App_(&app_, argc, argv);
    const int rc = run_App_(&app_);
    deinit_App(&app_);
    return rc;
}

void postRefresh_App(void) {
    iApp *d = &app_;
    if (!d->pendingRefresh) {
        d->pendingRefresh = iTrue;
        SDL_Event ev;
        ev.user.type     = SDL_USEREVENT;
        ev.user.code     = refresh_UserEventCode;
        ev.user.windowID = get_Window() ? SDL_GetWindowID(get_Window()->win) : 0;
        ev.user.data1    = NULL;
        ev.user.data2    = NULL;
        SDL_PushEvent(&ev);
    }
}

void postCommand_App(const char *command) {
    SDL_Event ev;
    ev.user.type     = SDL_USEREVENT;
    ev.user.code     = command_UserEventCode;
    ev.user.windowID = get_Window() ? SDL_GetWindowID(get_Window()->win) : 0;
    ev.user.data1    = strdup(command);
    ev.user.data2    = NULL;
    SDL_PushEvent(&ev);
#if !defined (NDEBUG)
    printf("[command] %s\n", command); fflush(stdout);
#endif
}

void postCommandf_App(const char *command, ...) {
    iBlock chars;
    init_Block(&chars, 0);
    va_list args;
    va_start(args, command);
    vprintf_Block(&chars, command, args);
    va_end(args);
    postCommand_App(cstr_Block(&chars));
    deinit_Block(&chars);
}

iAny *findWidget_App(const char *id) {
    return findChild_Widget(app_.window->root, id);
}

void addTicker_App(void (*ticker)(iAny *), iAny *context) {
    iApp *d = &app_;
    insert_SortedArray(&d->tickers, &(iTicker){ context, ticker });
}

static iBool handlePrefsCommands_(iWidget *d, const char *cmd) {
    if (equal_Command(cmd, "prefs.dismiss") || equal_Command(cmd, "preferences")) {
        setUiScale_Window(get_Window(),
                          toFloat_String(text_InputWidget(findChild_Widget(d, "prefs.uiscale"))));
        destroy_Widget(d);
        return iTrue;
    }
    return iFalse;
}

static iHistoryItem *historyItem_App_(iApp *d, size_t pos) {
    if (isEmpty_Array(&d->history)) return NULL;
    return &value_Array(&d->history, size_Array(&d->history) - 1 - pos, iHistoryItem);
}

static const iString *historyUrl_App_(iApp *d, size_t pos) {
    const iHistoryItem *item = historyItem_App_(d, pos);
    if (item) {
        return &item->url;
    }
    return collectNew_String();
}

static void printHistory_App_(const iApp *d) {
#if 0
    iConstForEach(Array, i, &d->history) {
        const size_t idx = index_ArrayConstIterator(&i);
        printf("%s[%zu]: %s\n",
               d->historyPos == size_Array(&d->history) - idx - 1 ? "->" : "  ",
               idx,
               cstr_String(&((const iHistoryItem *) i.value)->url));
    }
    fflush(stdout);
#endif
}

iBool handleCommand_App(const char *cmd) {
    iApp *d = &app_;
    iWidget *root = d->window->root;
    if (equal_Command(cmd, "open")) {
        const iString *url = collect_String(newCStr_String(suffixPtr_Command(cmd, "url")));
        if (!argLabel_Command(cmd, "history")) {
            if (argLabel_Command(cmd, "redirect")) {
                /* Update in the history. */
                iHistoryItem *item = historyItem_App_(d, d->historyPos);
                if (item) {
                    set_String(&item->url, url);
                }
            }
            else {
                /* Cut the trailing history items. */
                if (d->historyPos > 0) {
                    for (size_t i = 0; i < d->historyPos - 1; i++) {
                        deinit_HistoryItem(historyItem_App_(d, i));
                    }
                    removeN_Array(
                        &d->history, size_Array(&d->history) - d->historyPos, iInvalidSize);
                    d->historyPos = 0;
                }
                /* Insert new item. */
                const iHistoryItem *lastItem = historyItem_App_(d, 0);
                if (!lastItem || cmpString_String(&lastItem->url, url) != 0) {
                    iHistoryItem item;
                    init_HistoryItem(&item);
                    set_String(&item.url, url);
                    pushBack_Array(&d->history, &item);
                    /* Don't make it too long. */
                    if (size_Array(&d->history) > historyMax_App_) {
                        deinit_HistoryItem(front_Array(&d->history));
                        remove_Array(&d->history, 0);
                    }
                }
            }
        }
        printHistory_App_(d);
        setUrl_DocumentWidget(findChild_Widget(root, "document"), url);
    }
    else if (equal_Command(cmd, "document.request.cancelled")) {
        /* TODO: How should cancelled requests be treated in the history? */
#if 0
        if (d->historyPos == 0) {
            iHistoryItem *item = historyItem_App_(d, 0);
            if (item) {
                /* Pop this cancelled URL off history. */
                deinit_HistoryItem(item);
                popBack_Array(&d->history);
                printHistory_App_(d);
            }
        }
#endif
        return iFalse;
    }
    else if (equal_Command(cmd, "quit")) {
        SDL_Event ev;
        ev.type = SDL_QUIT;
        SDL_PushEvent(&ev);
    }
    else if (equal_Command(cmd, "preferences")) {
        iWindow *win = get_Window();
        iWidget *dlg = makePreferences_Widget();
        setToggle_Widget(findChild_Widget(dlg, "prefs.retainwindow"), d->retainWindowSize);
        setText_InputWidget(findChild_Widget(dlg, "prefs.uiscale"),
                            collectNewFormat_String("%g", uiScale_Window(get_Window())));
        setCommandHandler_Widget(dlg, handlePrefsCommands_);
    }
    else if (equal_Command(cmd, "restorewindow")) {
        d->retainWindowSize = iTrue;
        resize_Window(d->window, argLabel_Command(cmd, "width"), argLabel_Command(cmd, "height"));
        const iInt2 pos = coord_Command(cmd);
        SDL_SetWindowPosition(d->window->win, pos.x, pos.y);
    }
    else if (equal_Command(cmd, "document.changed")) {
        /* TODO: Update current history item with this actual/redirected URL. */
        return iFalse;
    }
    else if (equal_Command(cmd, "navigate.back")) {
        if (d->historyPos < size_Array(&d->history) - 1) {
            d->historyPos++;
            postCommandf_App("open history:1 url:%s",
                             cstr_String(historyUrl_App_(d, d->historyPos)));
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "navigate.forward")) {
        if (d->historyPos > 0) {
            d->historyPos--;
            postCommandf_App("open history:1 url:%s",
                             cstr_String(historyUrl_App_(d, d->historyPos)));
        }
        return iTrue;
    }
    else if (equal_Command(cmd, "navigate.home")) {
        iString *homePath = newCStr_String(dataDir_App_);
        clean_Path(homePath);
        append_Path(homePath, &iStringLiteral("home.gmi"));
        prependCStr_String(homePath, "file://");
        postCommandf_App("open url:%s", cstr_String(homePath));
        delete_String(homePath);
        return iTrue;
    }
    else {
        return iFalse;
    }
    return iTrue;
}
