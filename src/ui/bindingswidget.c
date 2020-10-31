/* Copyright 2020 Jaakko Keränen <jaakko.keranen@iki.fi>

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include "bindingswidget.h"
#include "listwidget.h"
#include "keys.h"
#include "command.h"
#include "util.h"
#include "app.h"

iDeclareType(BindingItem)
typedef iListItemClass iBindingItemClass;

struct Impl_BindingItem {
    iListItem listItem;
    iString   label;
    iString   key;
    int       id;
    iBool     isWaitingForEvent;
};

void init_BindingItem(iBindingItem *d) {
    init_ListItem(&d->listItem);
    init_String(&d->label);
    init_String(&d->key);
    d->id = 0;
    d->isWaitingForEvent = iFalse;
}

void deinit_BindingItem(iBindingItem *d) {
    deinit_String(&d->key);
    deinit_String(&d->label);
}

static void setKey_BindingItem_(iBindingItem *d, int key, int mods) {
    setKey_Binding(d->id, key, mods);
    clear_String(&d->key);
    toString_Sym(key, mods, &d->key);
}

static void draw_BindingItem_(const iBindingItem *d, iPaint *p, iRect itemRect,
                              const iListWidget *list);

iBeginDefineSubclass(BindingItem, ListItem)
    .draw = (iAny *) draw_BindingItem_,
iEndDefineSubclass(BindingItem)

iDefineObjectConstruction(BindingItem)

/*----------------------------------------------------------------------------------------------*/

struct Impl_BindingsWidget {
    iWidget widget;
    iListWidget *list;
    size_t activePos;
};

iDefineObjectConstruction(BindingsWidget)

static int cmpId_BindingItem_(const iListItem **item1, const iListItem **item2) {
    const iBindingItem *d = (const iBindingItem *) *item1, *other = (const iBindingItem *) *item2;
    return iCmp(d->id, other->id);
}

static void updateItems_BindingsWidget_(iBindingsWidget *d) {
    clear_ListWidget(d->list);
    iConstForEach(PtrArray, i, list_Keys()) {
        const iBinding *bind = i.ptr;
        if (isEmpty_String(&bind->label)) {
            /* Only the ones with label are user-changeable. */
            continue;
        }
        iBindingItem *item = new_BindingItem();
        item->id = bind->id;
        set_String(&item->label, &bind->label);
        toString_Sym(bind->key, bind->mods, &item->key);
        addItem_ListWidget(d->list, item);
    }
    sort_ListWidget(d->list, cmpId_BindingItem_);
    updateVisible_ListWidget(d->list);
    invalidate_ListWidget(d->list);
}

void init_BindingsWidget(iBindingsWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setFlags_Widget(w, resizeChildren_WidgetFlag, iTrue);
    d->activePos = iInvalidPos;
    d->list = new_ListWidget();
    setItemHeight_ListWidget(d->list, lineHeight_Text(uiLabel_FontId) * 1.5f);
    setPadding_Widget(as_Widget(d->list), 0, gap_UI, 0, gap_UI);
    addChild_Widget(w, iClob(d->list));
    updateItems_BindingsWidget_(d);
}

void deinit_BindingsWidget(iBindingsWidget *d) {
    /* nothing to do */
    iUnused(d);
}

static void setActiveItem_BindingsWidget_(iBindingsWidget *d, size_t pos) {
    if (d->activePos != iInvalidPos) {
        iBindingItem *item = item_ListWidget(d->list, d->activePos);
        item->isWaitingForEvent = iFalse;
        invalidateItem_ListWidget(d->list, d->activePos);
    }
    d->activePos = pos;
    if (d->activePos != iInvalidPos) {
        iBindingItem *item = item_ListWidget(d->list, d->activePos);
        item->isWaitingForEvent = iTrue;
        invalidateItem_ListWidget(d->list, d->activePos);
    }
}

static iBool processEvent_BindingsWidget_(iBindingsWidget *d, const SDL_Event *ev) {
    iWidget *   w   = as_Widget(d);
    const char *cmd = command_UserEvent(ev);
    if (isCommand_Widget(w, ev, "list.clicked")) {
        setActiveItem_BindingsWidget_(d, arg_Command(cmd));
        return iTrue;
    }
    /* Waiting for a keypress? */
    if (d->activePos != iInvalidPos) {
        if (ev->type == SDL_KEYDOWN && !isMod_Sym(ev->key.keysym.sym)) {
            setKey_BindingItem_(item_ListWidget(d->list, d->activePos),
                                ev->key.keysym.sym,
                                keyMods_Sym(ev->key.keysym.mod));
            setActiveItem_BindingsWidget_(d, iInvalidPos);
            postCommand_App("bindings.changed");
            return iTrue;
        }
    }
    return processEvent_Widget(w, ev);
}

static void draw_BindingsWidget_(const iBindingsWidget *d) {
    const iWidget *w = constAs_Widget(d);
    drawChildren_Widget(w);
    drawBackground_Widget(w); /* kludge to allow drawing a top border over the list */
}

static void draw_BindingItem_(const iBindingItem *d, iPaint *p, iRect itemRect,
                              const iListWidget *list) {
    const int   font       = uiLabel_FontId;
    const int   itemHeight = height_Rect(itemRect);
    const int   line       = lineHeight_Text(font);
    int         fg         = uiText_ColorId;
    const iBool isPressing = isMouseDown_ListWidget(list) || d->isWaitingForEvent;
    const iBool isHover    = (isHover_Widget(constAs_Widget(list)) &&
                              constHoverItem_ListWidget(list) == d);
    if (isHover || isPressing) {
        fg = isPressing ? uiTextPressed_ColorId : uiTextFramelessHover_ColorId;
        fillRect_Paint(p,
                       itemRect,
                       isPressing ? uiBackgroundPressed_ColorId
                                  : uiBackgroundFramelessHover_ColorId);
    }
    const int y = top_Rect(itemRect) + (itemHeight - line) / 2;
    drawRange_Text(font,
                   init_I2(left_Rect(itemRect) + 3 * gap_UI, y),
                   fg,
                   range_String(&d->label));
    drawAlign_Text(d->isWaitingForEvent ? uiContent_FontId : font,
                   init_I2(right_Rect(itemRect) - 3 * gap_UI,
                           y - (lineHeight_Text(uiContent_FontId) - line) / 2),
                   fg,
                   right_Alignment,
                   "%s",
                   d->isWaitingForEvent ? "\U0001F449 \u2328" : cstr_String(&d->key));
}

iBeginDefineSubclass(BindingsWidget, Widget)
    .processEvent = (iAny *) processEvent_BindingsWidget_,
    .draw         = (iAny *) draw_BindingsWidget_,
iEndDefineSubclass(BindingsWidget)
