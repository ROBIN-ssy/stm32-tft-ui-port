#ifndef KK_UI_INTERNAL_H
#define KK_UI_INTERNAL_H

#include "kk_ui.h"
#include "kk_oled.h"

#include <stddef.h>

#define KK_UI_Q12_ONE 4096U

#define KK_UI_HOME_MS             300U
#define KK_UI_MENU_MS             200U
#define KK_UI_PAGE_MS             220U
#define KK_UI_INFO_SCROLL_MS      180U
#define KK_UI_DIALOG_OPEN_MS      180U
#define KK_UI_DIALOG_CLOSE_MS     150U
#define KK_UI_FOCUS_MS            160U
#define KK_UI_INT_VALUE_MS        100U
#define KK_UI_BOOL_VALUE_MS       120U
#define KK_UI_TOAST_MS            120U

/*
 * 本工程按 kk-ui-port 的“其他尺寸”策略，从参考的 128x64 迁移到 320x240。
 * 迁移只集中在这些绘制常量与各页 Draw 函数；公共 API 与状态机未改动。
 * KK_OLED 上报的逻辑尺寸必须与下面两值完全一致，kk_ui.c 启动时会校验。
 */
#define KK_UI_SCREEN_WIDTH        320
#define KK_UI_SCREEN_HEIGHT       240
#define KK_UI_MENU_HEADER_HEIGHT  30
#define KK_UI_INFO_HEADER_HEIGHT  32
#define KK_UI_ROW_HEIGHT          32
#define KK_UI_VISIBLE_ROWS        6
#define KK_UI_PAGE_PARALLAX       96

/* 首页：32x32 图标保持参考尺寸，间距与纵向位置按 320x240 重新选取。 */
#define KK_UI_HOME_ICON_SIZE      32
#define KK_UI_HOME_ICON_SPACING   88
#define KK_UI_HOME_ICON_Y         52
#define KK_UI_HOME_LABEL_MAX_W    240
#define KK_UI_HOME_LABEL_Y        150
#define KK_UI_HOME_LABEL_H        36
#define KK_UI_HOME_LABEL_TEXT_Y   160

/* 普通菜单：左右留白、右侧数值区、滚动条列。 */
#define KK_UI_MENU_MARGIN_X       16
#define KK_UI_MENU_FOCUS_X        4
#define KK_UI_MENU_FOCUS_W        300
#define KK_UI_MENU_VALUE_RIGHT    292
#define KK_UI_MENU_STATE_X        284
#define KK_UI_MENU_ARROW_X        296
#define KK_UI_MENU_SCROLLBAR_X    308
#define KK_UI_MENU_SCROLLBAR_W    4

/* 信息页：左侧名称、右侧数值、滚动条列。 */
#define KK_UI_INFO_MARGIN_X       16
#define KK_UI_INFO_VALUE_RIGHT    292
#define KK_UI_INFO_SCROLLBAR_X    308
#define KK_UI_INFO_CLOSE_X        288
#define KK_UI_INFO_CLOSE_Y        8
#define KK_UI_INFO_CLOSE_SIZE     16

/* 模态框与 Toast。 */
#define KK_UI_DIALOG_X            8
#define KK_UI_DIALOG_Y            8
#define KK_UI_DIALOG_W            304
#define KK_UI_DIALOG_H            224
#define KK_UI_DIALOG_OPEN_H       24
#define KK_UI_DIALOG_TITLE_Y      16
#define KK_UI_DIALOG_TITLE_H      24
#define KK_UI_DIALOG_RULE_X       22
#define KK_UI_DIALOG_RULE_Y       52
#define KK_UI_DIALOG_RULE_W       276
#define KK_UI_DIALOG_VALUE_X      22
#define KK_UI_DIALOG_VALUE_Y      66
#define KK_UI_DIALOG_VALUE_W      244
#define KK_UI_DIALOG_VALUE_H      40
#define KK_UI_DIALOG_ARROW_X      282
#define KK_UI_DIALOG_SWITCH_W     60
#define KK_UI_DIALOG_SWITCH_H     28
#define KK_UI_DIALOG_TEXT_W       64
#define KK_UI_DIALOG_BTN_Y        176
#define KK_UI_DIALOG_BTN_W        88
#define KK_UI_DIALOG_BTN_H        36
#define KK_UI_DIALOG_CANCEL_X     52
#define KK_UI_DIALOG_CONFIRM_X    180
#define KK_UI_TOAST_H             32
#define KK_UI_TOAST_MAX_W         300

typedef struct {
    KK_UI_PageId page;
    uint16_t selected;
    uint16_t top;
    int32_t scroll_q8;
    int32_t focus_q8;
} KK_UI_PageState;

typedef struct {
    uint32_t changed_at;
    uint32_t pressed_at;
    uint32_t repeat_at;
    uint8_t candidate;
    uint8_t stable;
    uint8_t armed;
} KK_UI_KeyState;

typedef struct {
    int32_t scroll_from_q8;
    int32_t focus_from_q8;
    uint32_t started;
    uint16_t duration;
    uint8_t active;
} KK_UI_ListAnimation;

typedef struct {
    int32_t offset_from_q8;
    int32_t label_from_q8;
    int32_t label_to_q8;
    uint32_t started;
    uint8_t active;
} KK_UI_HomeAnimation;

typedef struct {
    KK_UI_PageState outgoing;
    uint32_t started;
    uint8_t active;
    uint8_t reverse;
    uint8_t leave_custom;
} KK_UI_PageTransition;

typedef enum {
    KK_UI_OVERLAY_NONE = 0,
    KK_UI_OVERLAY_INT,
    KK_UI_OVERLAY_BOOL,
    KK_UI_OVERLAY_CONFIRM,
    KK_UI_OVERLAY_MESSAGE
} KK_UI_OverlayType;

typedef enum {
    KK_UI_FOCUS_VALUE = 0,
    KK_UI_FOCUS_CONFIRM,
    KK_UI_FOCUS_CANCEL
} KK_UI_DialogFocus;

typedef struct {
    KK_UI_OverlayType type;
    uint16_t ref;
    const char *text;
    KK_UI_EventId event;
    int32_t draft;
    int32_t original;
    int32_t previous;
    int32_t focus_x_q8;
    int32_t focus_y_q8;
    int32_t focus_w_q8;
    int32_t focus_h_q8;
    int32_t focus_from_x_q8;
    int32_t focus_from_y_q8;
    int32_t focus_from_w_q8;
    int32_t focus_from_h_q8;
    uint32_t phase_started;
    uint32_t focus_started;
    uint32_t value_started;
    int16_t origin_y;
    uint16_t phase_q12;
    uint16_t phase_from_q12;
    uint16_t value_q12;
    uint8_t focus;
    int8_t value_direction;
    uint8_t editing;
    uint8_t closing;
    uint8_t focus_moving;
    uint8_t value_moving;
} KK_UI_Overlay;

typedef struct {
    const char *text;
    uint32_t shown_at;
    uint32_t expires_at;
    uint16_t phase_q12;
    uint8_t visible;
    uint8_t closing;
} KK_UI_Toast;

typedef enum {
    KK_UI_DEFER_NONE = 0,
    KK_UI_DEFER_CLOSE,
    KK_UI_DEFER_FINISH,
    KK_UI_DEFER_INT,
    KK_UI_DEFER_BOOL,
    KK_UI_DEFER_CONFIRM,
    KK_UI_DEFER_MESSAGE,
    KK_UI_DEFER_TOAST
} KK_UI_DeferredType;

typedef struct {
    KK_UI_DeferredType type;
    uint16_t ref;
    KK_UI_EventId event;
    const char *text;
    uint32_t duration;
} KK_UI_Deferred;

typedef struct {
    const KK_UI_App *app;
    KK_UI_PageState current;
    KK_UI_PageState stack[KK_UI_NAV_DEPTH];
    KK_UI_PageTransition transition;
    KK_UI_HomeAnimation home_anim;
    KK_UI_ListAnimation list_anim;
    KK_UI_Overlay overlay;
    KK_UI_Toast toast;
    KK_UI_Deferred deferred;
    KK_UI_KeyState keys[3];
    KK_UI_EventId events[KK_UI_EVENT_QUEUE_LENGTH];
    KK_UI_ErrorInfo error;
    uint32_t last_update;
    uint32_t next_frame;
    uint32_t transfer_started;
    uint8_t stack_count;
    uint8_t event_head;
    uint8_t event_count;
    uint8_t initialized;
    uint8_t keys_initialized;
    uint8_t transfer_active;
    uint8_t frame_ready;
    uint8_t dirty;
    uint8_t display_fault;
    uint8_t error_valid;
    uint8_t in_callback;
    uint8_t reject_flash;
    uint8_t blocked_keys;
} KK_UI_Runtime;

extern KK_UI_Runtime kk_ui;

uint16_t KK_UI_EaseQ12(uint32_t elapsed, uint32_t duration);
int32_t KK_UI_LerpQ12(int32_t from, int32_t to, uint16_t progress);
int16_t KK_UI_RoundQ8(int32_t value);

void KK_UI_RecordError(KK_UI_Status code, KK_UI_PageId page, uint16_t index);
bool KK_UI_QueueEvent(KK_UI_EventId event);
const KK_UI_PageRoute *KK_UI_GetRoute(KK_UI_PageId page);
KK_UI_PageType KK_UI_CurrentPageType(void);

void KK_UI_ResetPageState(KK_UI_PageState *state, KK_UI_PageId page);
KK_UI_Status KK_UI_NavigateTo(KK_UI_PageId page, uint32_t now);
KK_UI_Status KK_UI_NavigateBack(uint32_t now);
void KK_UI_AnimateNavigation(uint32_t now);
void KK_UI_DrawScene(uint32_t now);
void KK_UI_DrawPage(const KK_UI_PageState *state, int16_t x_offset,
                    int16_t clip_x, uint16_t clip_width, uint32_t now);

void KK_UI_HomeAnimate(uint32_t now);
void KK_UI_HomeInput(KK_UI_InputEvent event, uint32_t now);
void KK_UI_HomeDraw(const KK_UI_PageState *state, int16_t x_offset,
                    int16_t clip_x, uint16_t clip_width);

KK_UI_ItemState KK_UI_MenuGetItemState(const KK_UI_MenuPage *page,
                                       uint16_t item_index);
void KK_UI_MenuRepairFocus(KK_UI_PageState *state);
void KK_UI_MenuAnimate(uint32_t now);
void KK_UI_MenuInput(KK_UI_InputEvent event, uint32_t now);
void KK_UI_MenuDraw(const KK_UI_PageState *state, int16_t x_offset,
                    int16_t clip_x, uint16_t clip_width);

void KK_UI_InfoAnimate(uint32_t now);
void KK_UI_InfoInput(KK_UI_InputEvent event, uint32_t now);
void KK_UI_InfoDraw(const KK_UI_PageState *state, int16_t x_offset,
                    int16_t clip_x, uint16_t clip_width);
void KK_UI_CustomInput(KK_UI_InputEvent event);
void KK_UI_CustomTick(uint32_t now);

KK_UI_Status KK_UI_DialogOpenInt(uint16_t index, uint32_t now);
KK_UI_Status KK_UI_DialogOpenBool(uint16_t index, uint32_t now);
KK_UI_Status KK_UI_DialogOpenConfirm(uint16_t index, uint32_t now);
KK_UI_Status KK_UI_DialogOpenMessage(const char *text, KK_UI_EventId event,
                                     uint32_t now);
KK_UI_Status KK_UI_ToastOpen(const char *text, uint32_t duration,
                             uint32_t now);
void KK_UI_DialogInput(KK_UI_InputEvent event, uint32_t now);
void KK_UI_DialogAnimate(uint32_t now);
void KK_UI_DialogDraw(void);
void KK_UI_ToastDraw(void);
bool KK_UI_DialogActive(void);

void KK_UI_DispatchInput(KK_UI_InputEvent event, uint32_t now);
void KK_UI_ApplyDeferred(uint32_t now);
KK_UI_Status KK_UI_Defer(KK_UI_DeferredType type, uint16_t ref,
                         const char *text, KK_UI_EventId event,
                         uint32_t duration);

void KK_UI_FormatInt(int32_t value, const char *unit, char *buffer,
                     size_t capacity);
void KK_UI_DrawCentered(int16_t x, int16_t y, uint16_t width,
                        const char *text);

#endif
