#define LOG_TAG "HUD"
#include "../log.h"
#include "hud.h"
#include "nav16_rx.h"        // receiver lifecycle/seen + re-exported hud_nav16 API
                             // (HudNav16Sink, AaGuidance/Position/Status, AaLane, mappers)
#include "common/config.h"   // libpatch_config::hud_transport()


#include "svcnavi_tx.h"
#include "vbs_tx.h"
#include "translit.h"   // hud_translit::fold() — precomposed-Latin street-name fold
#include "hud_nav.h"    // compute_turn_icon() — AA turn fields -> Mazda HUD glyph
#include "hud_lane.h"   // oem_lane_code_for_aa — AA lanes -> OEM lane codes

#include <stdint.h>
#include <string.h>

namespace {


struct HudTransportOps {
    void (*start)();
    void (*stop)();
    void (*status)(uint32_t status);
    void (*next_turn)(const char *road, uint32_t icon);
    void (*distance)(int32_t dist_dec, uint8_t dist_unit);
    void (*lanes)(const uint8_t *codes);             // raw setter; fed by emit_lane_codes
};


void svcnavi_next_turn_adapter(const char *road, uint32_t icon)
{
    svcnavi_tx_next_turn(road, icon);
}
void vbs_next_turn_adapter(const char *road, uint32_t icon)
{
    vbs_tx_next_turn(road, icon);
}

//   1  = recommended lane marker (small triangle)
//   28 = non-recommended lane (blank triangle, no marker)
//   0  = no lane in this slot
void nav16_encode_lane_codes(const AaLane *lanes, int n, uint8_t out[HUD_NAV16_MAX_LANES])
{
    for (int i = 0; i < HUD_NAV16_MAX_LANES; ++i) {
        if (lanes && i < n) {
            bool is_recommended = (lanes[i].highlight_mask != 0);
            out[i] = is_recommended ? 1 : 28;
        } else {
            out[i] = 0;
        }
    }
}

const HudTransportOps kSvcnaviOps = {
    &svcnavi_tx_start, &svcnavi_tx_stop, &svcnavi_tx_status,
    &svcnavi_next_turn_adapter, &svcnavi_tx_distance, &svcnavi_tx_lanes,
};
const HudTransportOps kVbsOps = {
    &vbs_tx_start, &vbs_tx_stop, &vbs_tx_status,
    &vbs_next_turn_adapter, &vbs_tx_distance, &vbs_tx_lanes,
};


const HudTransportOps *g_tx = &kSvcnaviOps;

inline void hud_tx_start()
{
    g_tx = (libpatch_config::hud_transport() == libpatch_config::HUD_TRANSPORT_VBS)
               ? &kVbsOps : &kSvcnaviOps;
    g_tx->start();
}
inline void hud_tx_stop()   { g_tx->stop(); }

inline void hud_tx_status(uint32_t status) { g_tx->status(status); }
inline void hud_tx_next_turn(const char *road, uint32_t side, uint32_t event,
                             int32_t angle, int32_t /*number*/)
{
    
    uint32_t icon = compute_turn_icon(event, side, angle);

    
    if (road != nullptr && libpatch_config::hud_fold_latin()) {
        char buf[256];
        strncpy(buf, road, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        hud_translit::fold(buf);
        g_tx->next_turn(buf, icon);
        return;
    }
    g_tx->next_turn(road, icon);
}
inline void hud_tx_distance(int32_t disp_dist, uint32_t disp_unit)
{
    
    g_tx->distance(disp_dist / 100, map_distance_unit(disp_unit));
}


constexpr int kCbListWordSlots = 19;       // 0x4c bytes
constexpr int kCbListNavSlot   = 10;       // byte offset 0x28 — E_AAP_EVENT_NAV_DATA_CB
constexpr int kCbListUserSlot  = 18;       // byte offset 0x48 — passed as first arg


// emits 0x502 from a NAVDistanceMessage).
constexpr uint32_t kTagStatus    = 0x500;   // NAVMessagesStatus
constexpr uint32_t kTagNextTurn  = 0x501;   // NAVTurnMessage
constexpr uint32_t kTagDistance  = 0x502;   // NAVDistanceMessage

enum NavStatusEnum : uint32_t {
    NAV_STATUS_START = 1,
    NAV_STATUS_STOP  = 2,
};

enum NavTurnSideEnum : uint32_t {
    NAV_TURN_LEFT        = 1,
    NAV_TURN_RIGHT       = 2,
    NAV_TURN_UNSPECIFIED = 3,
};

enum NavTurnEventEnum : uint32_t {
    NAV_TURN_EVENT_UNKNOWN                  =  0,
    NAV_TURN_EVENT_DEPART                   =  1,
    NAV_TURN_EVENT_NAME_CHANGE              =  2,
    NAV_TURN_EVENT_SLIGHT_TURN              =  3,
    NAV_TURN_EVENT_TURN                     =  4,
    NAV_TURN_EVENT_SHARP_TURN               =  5,
    NAV_TURN_EVENT_U_TURN                   =  6,
    NAV_TURN_EVENT_ON_RAMP                  =  7,
    NAV_TURN_EVENT_OFF_RAMP                 =  8,
    NAV_TURN_EVENT_FORK                     =  9,
    NAV_TURN_EVENT_MERGE                    = 10,
    NAV_TURN_EVENT_ROUNDABOUT_ENTER         = 11,
    NAV_TURN_EVENT_ROUNDABOUT_EXIT          = 12,
    NAV_TURN_EVENT_ROUNDABOUT_ENTER_AND_EXIT= 13,
    NAV_TURN_EVENT_STRAIGHT                 = 14,
    NAV_TURN_EVENT_FERRY_BOAT               = 16,
    NAV_TURN_EVENT_FERRY_TRAIN              = 17,
    NAV_TURN_EVENT_DESTINATION              = 19,
};


uint32_t decode_turn_event(uint32_t compacted)
{
    switch (compacted) {
    case 15: return NAV_TURN_EVENT_FERRY_BOAT;   // 15 -> 16
    case 16: return NAV_TURN_EVENT_FERRY_TRAIN;  // 16 -> 17
    case 17: return NAV_TURN_EVENT_DESTINATION;  // 17 -> 19
    default: return compacted;
    }
}

enum NavDistanceUnitEnum : uint32_t {
    NAV_DISTUNIT_METERS       = 1,
    NAV_DISTUNIT_KILOMETERS10 = 2,
    NAV_DISTUNIT_KILOMETERS   = 3,
    NAV_DISTUNIT_MILES10      = 4,
    NAV_DISTUNIT_MILES        = 5,
    NAV_DISTUNIT_FEET         = 6,
};

struct NextTurnHdr {
    uint32_t    tag;
    const char *road_name;
    uint32_t    road_name_len;
    uint32_t    turn_side;
    uint32_t    turn_event;
    const void *image;
    uint32_t    image_len;
    int32_t     turn_angle;
    int32_t     turn_number;
};
static_assert(sizeof(NextTurnHdr) == 36, "NextTurnHdr must match 36-byte SDK buffer");

struct StatusHdr {
    uint32_t tag;
    uint32_t status;
    uint32_t reserved[7];
};
static_assert(sizeof(StatusHdr) == 36, "StatusHdr must match 36-byte SDK buffer");

// 0x502 header — distance / ETA. Last 16 bytes reserved.
struct DistanceHdr {
    uint32_t tag;
    int32_t  distance;
    int32_t  time_until;
    int32_t  display_distance;
    uint32_t display_distance_unit;
    uint32_t reserved[4];
};
static_assert(sizeof(DistanceHdr) == 36, "DistanceHdr must match 36-byte SDK buffer");

const char *nav_status_name(uint32_t v)
{
    switch (v) {
    case NAV_STATUS_START: return "START";
    case NAV_STATUS_STOP:  return "STOP";
    default:               return "?";
    }
}

const char *nav_turn_side_name(uint32_t v)
{
    switch (v) {
    case NAV_TURN_LEFT:        return "LEFT";
    case NAV_TURN_RIGHT:       return "RIGHT";
    case NAV_TURN_UNSPECIFIED: return "UNSPECIFIED";
    default:                   return "?";
    }
}

const char *nav_turn_event_name(uint32_t v)
{
    switch (v) {
    case NAV_TURN_EVENT_UNKNOWN:                   return "UNKNOWN";
    case NAV_TURN_EVENT_DEPART:                    return "DEPART";
    case NAV_TURN_EVENT_NAME_CHANGE:               return "NAME_CHANGE";
    case NAV_TURN_EVENT_SLIGHT_TURN:               return "SLIGHT_TURN";
    case NAV_TURN_EVENT_TURN:                      return "TURN";
    case NAV_TURN_EVENT_SHARP_TURN:                return "SHARP_TURN";
    case NAV_TURN_EVENT_U_TURN:                    return "U_TURN";
    case NAV_TURN_EVENT_ON_RAMP:                   return "ON_RAMP";
    case NAV_TURN_EVENT_OFF_RAMP:                  return "OFF_RAMP";
    case NAV_TURN_EVENT_FORK:                      return "FORK";
    case NAV_TURN_EVENT_MERGE:                     return "MERGE";
    case NAV_TURN_EVENT_ROUNDABOUT_ENTER:          return "ROUNDABOUT_ENTER";
    case NAV_TURN_EVENT_ROUNDABOUT_EXIT:           return "ROUNDABOUT_EXIT";
    case NAV_TURN_EVENT_ROUNDABOUT_ENTER_AND_EXIT: return "ROUNDABOUT_ENTER_AND_EXIT";
    case NAV_TURN_EVENT_STRAIGHT:                  return "STRAIGHT";
    case NAV_TURN_EVENT_FERRY_BOAT:                return "FERRY_BOAT";
    case NAV_TURN_EVENT_FERRY_TRAIN:               return "FERRY_TRAIN";
    case NAV_TURN_EVENT_DESTINATION:               return "DESTINATION";
    default:                                       return "?";
    }
}

const char *nav_distance_unit_name(uint32_t v)
{
    switch (v) {
    case NAV_DISTUNIT_METERS:       return "METERS";
    case NAV_DISTUNIT_KILOMETERS10: return "KILOMETERS10";
    case NAV_DISTUNIT_KILOMETERS:   return "KILOMETERS";
    case NAV_DISTUNIT_MILES10:      return "MILES10";
    case NAV_DISTUNIT_MILES:        return "MILES";
    case NAV_DISTUNIT_FEET:         return "FEET";
    default:                        return "?";
    }
}

// Forward declaration so substitute_nav_cb() below can take its
// address before the body is seen.
void our_nav_cb(void *user_ctx, void *hdr36);

void dump_status(const StatusHdr *h)
{
    LOGD("nav 0x500 NAVMessagesStatus: status=%u(%s)",
         static_cast<unsigned>(h->status),
         nav_status_name(h->status));
}

void dump_next_turn(const NextTurnHdr *h, uint32_t turn_event)
{
    constexpr size_t kRoadCap = 255;
    char road[kRoadCap + 1];
    road[0] = '\0';
    if (h->road_name && h->road_name_len) {
        size_t n = h->road_name_len;
        if (n > kRoadCap) n = kRoadCap;
        memcpy(road, h->road_name, n);
        road[n] = '\0';
    }

    LOGD("nav 0x501 NAVTurnMessage: road=\"%s\" road_len=%u "
         "turn_side=%u(%s) turn_event=%u(%s) raw_event=%u "
         "turn_angle=%d turn_number=%d image=%p image_len=%u",
         road,
         static_cast<unsigned>(h->road_name_len),
         static_cast<unsigned>(h->turn_side),
         nav_turn_side_name(h->turn_side),
         static_cast<unsigned>(turn_event),
         nav_turn_event_name(turn_event),
         static_cast<unsigned>(h->turn_event),
         static_cast<int>(h->turn_angle),
         static_cast<int>(h->turn_number),
         h->image,
         static_cast<unsigned>(h->image_len));

    if (h->image_len != 0 || h->image != nullptr) {
        LOGW("nav 0x501: unexpected image payload in IMAGE_CODES_ONLY "
             "mode (image=%p image_len=%u) — check "
             "<cluster_type>ENUM</> in aap_system_attributes*.xml "
             "(image bytes are PNG-encoded per hu.proto)",
             h->image, static_cast<unsigned>(h->image_len));
    }
}

void dump_distance(const DistanceHdr *h)
{
    LOGD("nav 0x502 NAVDistanceMessage: distance=%dm time_until=%ds "
         "display_distance=%d display_distance_unit=%u(%s)",
         static_cast<int>(h->distance),
         static_cast<int>(h->time_until),
         static_cast<int>(h->display_distance),
         static_cast<unsigned>(h->display_distance_unit),
         nav_distance_unit_name(h->display_distance_unit));
}

void our_nav_cb(void *user_ctx, void *hdr36)
{
    (void)user_ctx;

    if (!hdr36) {
        LOGW("nav cb: NULL header — SDK contract violation, ignoring");
        return;
    }

    if (hud_nav16_rx_seen()) return;

    const uint32_t tag = *static_cast<const uint32_t *>(hdr36);
    switch (tag) {
    case kTagStatus: {
        const StatusHdr *s = static_cast<const StatusHdr *>(hdr36);
        dump_status(s);
        hud_tx_status(s->status);
        break;
    }
    case kTagNextTurn: {
        const NextTurnHdr *t = static_cast<const NextTurnHdr *>(hdr36);
        const uint32_t turn_event = decode_turn_event(t->turn_event);
        dump_next_turn(t, turn_event);
        hud_tx_next_turn(t->road_name, t->turn_side, turn_event,
                         t->turn_angle, t->turn_number);
        break;
    }
    case kTagDistance: {
        const DistanceHdr *d = static_cast<const DistanceHdr *>(hdr36);
        dump_distance(d);
        hud_tx_distance(d->display_distance, d->display_distance_unit);
        break;
    }
    default:
        LOGW("nav cb: unknown tag 0x%x — first 16 bytes: "
             "%02x %02x %02x %02x  %02x %02x %02x %02x  "
             "%02x %02x %02x %02x  %02x %02x %02x %02x",
             tag,
             static_cast<const uint8_t *>(hdr36)[0],
             static_cast<const uint8_t *>(hdr36)[1],
             static_cast<const uint8_t *>(hdr36)[2],
             static_cast<const uint8_t *>(hdr36)[3],
             static_cast<const uint8_t *>(hdr36)[4],
             static_cast<const uint8_t *>(hdr36)[5],
             static_cast<const uint8_t *>(hdr36)[6],
             static_cast<const uint8_t *>(hdr36)[7],
             static_cast<const uint8_t *>(hdr36)[8],
             static_cast<const uint8_t *>(hdr36)[9],
             static_cast<const uint8_t *>(hdr36)[10],
             static_cast<const uint8_t *>(hdr36)[11],
             static_cast<const uint8_t *>(hdr36)[12],
             static_cast<const uint8_t *>(hdr36)[13],
             static_cast<const uint8_t *>(hdr36)[14],
             static_cast<const uint8_t *>(hdr36)[15]);
        break;
    }
}

} // namespace

void hud_pre_aap_create_session(void *cb_list)
{
    if (!cb_list) {
        LOGW("hud_pre_aap_create_session: NULL cb_list, skipping "
             "(real aap_create_session will reject it anyway)");
        return;
    }

    void **slots = static_cast<void **>(cb_list);
    void  *prev  = slots[kCbListNavSlot];
    slots[kCbListNavSlot] = reinterpret_cast<void *>(&our_nav_cb);

    LOGD("hud_pre_aap_create_session: cb_list=%p slot[%d] %p -> %p (our_nav_cb)",
         cb_list, kCbListNavSlot, prev,
         reinterpret_cast<void *>(&our_nav_cb));

    static_assert(kCbListUserSlot < kCbListWordSlots,
                  "user-context slot out of cb_list bounds");
}

enum { NAV_UNAVAILABLE = 0, NAV_ACTIVE = 1, NAV_INACTIVE = 2, NAV_REROUTING = 3 };

int32_t quantize_dist_x10(int32_t v, uint8_t mazda_unit)
{
    if (v < 0) return 0;
    if (mazda_unit == 2 || mazda_unit == 3) return v;
    if (v >= 5000) return (v + 250) / 500 * 500;
    if (v >= 1000) return (v +  50) / 100 * 100;
    if (v >=  200) return (v +  25) /  50 *  50;
    return v;
}

struct AaNav16HudState {
    uint32_t glyph;
    int32_t  dist_dec;
    uint8_t  dist_unit;
    uint8_t  n_lanes;
    char     road[64];
    AaLane   lanes[HUD_NAV16_MAX_LANES];
};

static AaNav16HudState g_nav16_acc;
static AaNav16HudState g_nav16_last;
static bool g_nav16_have_last = false;

void hud_feed_nav16_reset(void)
{
    memset(&g_nav16_acc, 0, sizeof(g_nav16_acc));
    g_nav16_have_last = false;
    LOGV("nav: accumulator + change-gate reset");
}

static void nav16_emit_if_changed()
{
    AaNav16HudState &acc = g_nav16_acc;
    if (g_nav16_have_last && memcmp(&acc, &g_nav16_last, sizeof(acc)) == 0) return;
    uint8_t lane_codes[HUD_NAV16_MAX_LANES];
    nav16_encode_lane_codes(acc.lanes, acc.n_lanes, lane_codes);
    g_tx->next_turn(acc.road, acc.glyph);
    g_tx->distance(acc.dist_dec, acc.dist_unit);
    g_tx->lanes(lane_codes);
    g_nav16_last      = acc;
    g_nav16_have_last = true;
}

static void nav16_on_guidance(const AaGuidance *g)
{
#if LOG_LEVEL <= LOG_LEVEL_VERBOSE
    char line[320]; hud_nav16_format_guidance(g, line, sizeof(line)); LOGV("%s", line);
#endif
    AaNav16HudState &acc = g_nav16_acc;

    uint32_t glyph = hud_nav16_glyph(g);
    if (glyph > 60) glyph = 0;

    char road[sizeof(acc.road)];
    strncpy(road, g->road, sizeof(road) - 1);
    road[sizeof(road) - 1] = '\0';
    if (libpatch_config::hud_fold_latin()) hud_translit::fold(road);

    if (glyph != acc.glyph || strcmp(road, acc.road) != 0) {
        acc.dist_dec  = 0;
        acc.dist_unit = 0;
    }
    acc.glyph = glyph;
    strncpy(acc.road, road, sizeof(acc.road) - 1);
    acc.road[sizeof(acc.road) - 1] = '\0';
    int nl = g->n_lanes;
    if (nl < 0) nl = 0;
    if (nl > HUD_NAV16_MAX_LANES) nl = HUD_NAV16_MAX_LANES;
    acc.n_lanes = (uint8_t)nl;
    for (int i = 0; i < HUD_NAV16_MAX_LANES; ++i) {
        acc.lanes[i].present_mask   = (i < nl) ? g->lanes[i].present_mask   : 0;
        acc.lanes[i].highlight_mask = (i < nl) ? g->lanes[i].highlight_mask : 0;
    }
    nav16_emit_if_changed();
}

static void nav16_on_position(const AaPosition *p)
{
#if LOG_LEVEL <= LOG_LEVEL_VERBOSE
    char line[320]; hud_nav16_format_position(p, line, sizeof(line)); LOGV("%s", line);
#endif
    if (!p->have_step) return;
    uint8_t unit = aa_to_mazda_unit(p->step_units);
    if (unit > 5) unit = 0;
    g_nav16_acc.dist_unit = unit;
    g_nav16_acc.dist_dec  = quantize_dist_x10(parse_dist_x10(p->step_display), unit);
    nav16_emit_if_changed();
}

static void nav16_on_status(const AaStatus *s)
{
    if (s->cluster_stop) {
        hud_tx_status(2);
        hud_feed_nav16_reset();
        LOGD("nav: cluster STOP (0x8002) -> HUD cleared");
        return;
    }
    if (s->nav_status == NAV_INACTIVE || s->nav_status == NAV_UNAVAILABLE) {
        hud_tx_status(2);
        hud_feed_nav16_reset();
        LOGD("nav: NavigationStatus=%d -> HUD cleared", s->nav_status);
    } else {
        LOGV("nav: NavigationStatus=%d (active/rerouting) -> HUD kept", s->nav_status);
    }
}

void hud_post_aap_create_session(void)
{
    hud_tx_start();
    if (libpatch_config::use_protocol_v1_6()) {
        hud_nav16_rx_start(&nav16_on_guidance, &nav16_on_position, &nav16_on_status);
    }
}

void hud_pre_aap_destroy_session(void)
{
    if (libpatch_config::use_protocol_v1_6()) {
        hud_nav16_rx_stop();
    }
    hud_tx_stop();
}
