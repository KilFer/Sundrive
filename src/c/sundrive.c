#include <pebble.h>
#include <locale.h>

// Main window and layers
static Window *s_window;
static Layer *s_canvas_layer;
static TextLayer *s_date_layer;

// Display properties
static GRect s_bounds;
static GPoint s_center;
static int16_t s_radius;
static bool s_is_round;

// Icons
static GBitmap *s_battery_icon_bitmap;
static GBitmap *s_steps_icon_bitmap;

// Date configuration
typedef struct {
  bool date_format_us;      // false = DD/MM, true = MM/DD
  bool show_day_of_week;    // true = show "Mo ", false = hide
} DateConfig;

static DateConfig s_date_config;
static char s_date_buffer[16];

// Step tracker
static int s_step_goal = 8000; 
static int s_current_steps = 0;
static bool s_show_hour_numbers = false;

// Twilight data (minutes since midnight UTC)
typedef struct {
  int16_t astronomical_twilight_begin;
  int16_t nautical_twilight_begin;
  int16_t civil_twilight_begin;
  int16_t sunrise;
  int16_t sunset;
  int16_t civil_twilight_end;
  int16_t nautical_twilight_end;
  int16_t astronomical_twilight_end;
  bool valid;
} TwilightData;

static TwilightData s_twilight;

// Persistent storage keys
#define STORAGE_KEY_TWILIGHT 1
#define STORAGE_KEY_DATE_CONFIG 2

#define STORAGE_KEY_TWILIGHT 1
#define STORAGE_KEY_DATE_CONFIG 2
#define STORAGE_KEY_STEP_GOAL 3
#define STORAGE_KEY_SHOW_HOUR_NUMBERS 4

// Color palettes for different platforms
#ifdef PBL_COLOR
  #define COLOR_DAY GColorCyan
  #define COLOR_CIVIL_TWILIGHT GColorRajah
  #define COLOR_NAUTICAL_TWILIGHT GColorCobaltBlue
  #define COLOR_ASTRONOMICAL_TWILIGHT GColorDukeBlue
  #define COLOR_NIGHT GColorBlack
  #define COLOR_BACKGROUND GColorBlack
  #define COLOR_HOUR_HAND GColorRed
  #define COLOR_MINUTE_HAND GColorRed
  #define COLOR_MARKS GColorWhite
  #define COLOR_BATTERY_HIGH GColorGreen
  #define COLOR_BATTERY_MEDIUM GColorYellow
  #define COLOR_BATTERY_LOW GColorRed
  #define COLOR_CHARGING GColorWhite
  #define COLOR_SEPARATOR GColorWhite
  #define COLOR_STEP_TRACKER GColorJazzberryJam
#else
  #define COLOR_DAY GColorWhite
  #define COLOR_CIVIL_TWILIGHT GColorLightGray
  #define COLOR_NAUTICAL_TWILIGHT GColorDarkGray
  #define COLOR_ASTRONOMICAL_TWILIGHT GColorDarkGray
  #define COLOR_NIGHT GColorBlack
  #define COLOR_BACKGROUND GColorBlack
  #define COLOR_HOUR_HAND_OVER_DAY GColorBlack
  #define COLOR_HOUR_HAND_OVER_NIGHT GColorWhite
  #define COLOR_MINUTE_HAND GColorWhite
  #define COLOR_MINUTE_HAND_OVER_DAY GColorBlack
  #define COLOR_MINUTE_HAND_OVER_NIGHT GColorWhite
  #define COLOR_MARKS GColorWhite
  #define COLOR_BATTERY_HIGH GColorLightGray
  #define COLOR_BATTERY_MEDIUM GColorLightGray
  #define COLOR_BATTERY_LOW GColorLightGray
  #define COLOR_CHARGING GColorDarkGray
  #define COLOR_SEPARATOR GColorWhite
  #define COLOR_STEP_TRACKER GColorDarkGray
#endif

#define BATTERY_RING_WIDTH 10
#define STEP_TRACKER_WIDTH 10
#define SEPARATOR_WIDTH 1
#define TWILIGHT_RING_WIDTH 20

// Convert minutes since midnight to angle (0° = top/noon, 180° = bottom/midnight)
static int32_t minutes_to_angle(int minutes) {
  // 24 hour clock: 1440 minutes per full rotation
  // Subtract 720 to make noon (720 minutes) = 0°
  int adjusted = minutes - 720; 
  if (adjusted < 0) adjusted += 1440; 
  
  // Convert to angle: 1440 minutes = 360°, so 4 minutes = 1°
  int degree = (adjusted * 360) / 1440;
  return DEG_TO_TRIGANGLE(degree); 
}

// Get current time in minutes since midnight
//static int get_current_minutes() {
//  time_t now = time(NULL);
//  struct tm *t = localtime(&now);
//  return t->tm_hour * 60 + t->tm_min;
//}

// Determine period type based on current time
typedef enum {
  PERIOD_NIGHT,
  PERIOD_ASTRONOMICAL_TWILIGHT_DAWN,
  PERIOD_NAUTICAL_TWILIGHT_DAWN,
  PERIOD_CIVIL_TWILIGHT_DAWN,
  PERIOD_DAY,
  PERIOD_CIVIL_TWILIGHT_DUSK,
  PERIOD_NAUTICAL_TWILIGHT_DUSK,
  PERIOD_ASTRONOMICAL_TWILIGHT_DUSK
} PeriodType;

static PeriodType get_current_period(int current_minutes) {
  if (!s_twilight.valid) return PERIOD_DAY;
  
  if (current_minutes >= s_twilight.sunrise && current_minutes < s_twilight.sunset) {
    return PERIOD_DAY;
  } else if (current_minutes >= s_twilight.civil_twilight_begin && current_minutes < s_twilight.sunrise) {
    return PERIOD_CIVIL_TWILIGHT_DAWN;
  } else if (current_minutes >= s_twilight.sunset && current_minutes < s_twilight.civil_twilight_end) {
    return PERIOD_CIVIL_TWILIGHT_DUSK;
  } else if (current_minutes >= s_twilight.nautical_twilight_begin && current_minutes < s_twilight.civil_twilight_begin) {
    return PERIOD_NAUTICAL_TWILIGHT_DAWN;
  } else if (current_minutes >= s_twilight.civil_twilight_end && current_minutes < s_twilight.nautical_twilight_end) {
    return PERIOD_NAUTICAL_TWILIGHT_DUSK;
  } else if (current_minutes >= s_twilight.astronomical_twilight_begin && current_minutes < s_twilight.nautical_twilight_begin) {
    return PERIOD_ASTRONOMICAL_TWILIGHT_DAWN;
  } else if (current_minutes >= s_twilight.nautical_twilight_end && current_minutes < s_twilight.astronomical_twilight_end) {
    return PERIOD_ASTRONOMICAL_TWILIGHT_DUSK;
  } else {
    return PERIOD_NIGHT;
  }
}

// Update date display
static void update_date_display() {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  
  // Format date based on configuration
  if (s_date_config.show_day_of_week) {
    if (s_date_config.date_format_us) {
      // US format: "Mo 01/12"
      strftime(s_date_buffer, sizeof(s_date_buffer), "%a %m/%d", t);
    } else {
      // European format: "Mo 12/01"
      strftime(s_date_buffer, sizeof(s_date_buffer), "%a %d/%m", t);
    }
  } else {
    if (s_date_config.date_format_us) {
      // US format without day: "01/12"
      strftime(s_date_buffer, sizeof(s_date_buffer), "%m/%d", t);
    } else {
      // European format without day: "12/01"
      strftime(s_date_buffer, sizeof(s_date_buffer), "%d/%m", t);
    }
  }
  
  text_layer_set_text(s_date_layer, s_date_buffer);
}

// Get current step count
static void get_step_count() {
  if (s_step_goal == 0) return; // Disabled

  HealthMetric metric = HealthMetricStepCount;
  time_t start = time_start_of_today();
  time_t end = time(NULL);

  // Check the metric has data available for today
  HealthServiceAccessibilityMask mask = health_service_metric_accessible(metric, start, end);

  if (mask & HealthServiceAccessibilityMaskAvailable) {
    // Data is available!
    s_current_steps = (int)health_service_sum_today(metric);
  } else {
    // No data available
    s_current_steps = 0;
  }

  // Local test: s_current_steps = 2700;
}

// Health event handler
static void health_handler(HealthEventType event, void *context) {
  if (event == HealthEventMovementUpdate) {
    get_step_count();
    if (s_canvas_layer) {
      layer_mark_dirty(s_canvas_layer);
    }
  }
}

// Draw step tracker
static void draw_step_tracker(GContext *ctx) {
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Drawing step traker. Steps: %d for limit: %d", (int)s_current_steps ,(int)s_step_goal);

  if (s_step_goal == 0) return; // Disabled

  // Calculate radius: Inside battery ring
  int16_t tracker_radius = s_radius - TWILIGHT_RING_WIDTH - SEPARATOR_WIDTH;

  GRect tracker_box = GRect(s_center.x - tracker_radius, s_center.y - tracker_radius,
                            tracker_radius * 2, tracker_radius * 2);

  // Calculate fill percentage
  int steps = s_current_steps;
  if (steps > s_step_goal) steps = s_step_goal;
  
  int32_t angle_270 = DEG_TO_TRIGANGLE(270);
  int32_t max_span = TRIG_MAX_ANGLE / 2; // 180 degrees
  
  int32_t current_span = (int32_t)steps * max_span / s_step_goal;
  
  // Handle overflow if steps > goal
  if (current_span > max_span) current_span = max_span;

  int32_t start_angle = angle_270 - current_span;
  
  
  graphics_context_set_fill_color(ctx, COLOR_STEP_TRACKER);
  graphics_fill_radial(ctx, tracker_box, GOvalScaleModeFitCircle, STEP_TRACKER_WIDTH, 
                      start_angle, angle_270);
}

// Draw battery indicator
static void draw_battery_indicator(GContext *ctx) {
  // Get battery state
  BatteryChargeState battery_state = battery_state_service_peek();
  uint8_t battery_percent = battery_state.charge_percent;
  bool is_charging = battery_state.is_charging;
  
  // Calculate inner radius for battery ring
  // Twilight ring is 20px from edge, separator is 1px, so battery starts at 21px from edge
  int16_t battery_radius = s_radius - TWILIGHT_RING_WIDTH - SEPARATOR_WIDTH;
  
  // Create smaller bounding box for battery indicator
  GRect battery_box = GRect(s_center.x - battery_radius, s_center.y - battery_radius, 
                            battery_radius * 2, battery_radius * 2);
  
  
  // First, fill the entire battery ring area with black (background)
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_radial(ctx, battery_box, GOvalScaleModeFitCircle, BATTERY_RING_WIDTH, 
                      0, TRIG_MAX_ANGLE);
  
  // If charging, color the top semicircle background with GColorChromeYellow
  // Top semicircle spans from 270° (left) to 90° (right), crossing 0° at the top
  if (is_charging) {
      graphics_context_set_fill_color(ctx, COLOR_CHARGING);
    
    // Split the arc at 0° boundary: 270° to 360°, then 0° to 90°
    int32_t left_angle = DEG_TO_TRIGANGLE(270);  // Left side
    int32_t right_angle = DEG_TO_TRIGANGLE(90);   // Right side
    
    // Draw from left to top (270° to 360°/0°)
    graphics_fill_radial(ctx, battery_box, GOvalScaleModeFitCircle, BATTERY_RING_WIDTH, 
                        left_angle, TRIG_MAX_ANGLE);
    // Draw from top to right (0° to 90°)
    graphics_fill_radial(ctx, battery_box, GOvalScaleModeFitCircle, BATTERY_RING_WIDTH, 
                        0, right_angle);
  }
  
  // Determine battery color based on percentage
  GColor battery_color;
  if (battery_percent >= 50) {
    battery_color = COLOR_BATTERY_HIGH;
  } else if (battery_percent >= 21) {
    battery_color = COLOR_BATTERY_MEDIUM;
  } else {
    battery_color = COLOR_BATTERY_LOW;
  }
  
  // Calculate angles for battery indicator
  // Top semicircle: from left (270°) through top (0°) to right (90°)
  // Left is at 270° = -90° from top = TRIG_MAX_ANGLE * 3/4
  int32_t battery_start = DEG_TO_TRIGANGLE(270);  // Left side
  
  // Battery percentage determines how far around the semicircle we go
  // 0% = left (270°), 50% = top (0°/360°), 100% = right (90°)
  // So percentage maps to degrees: 270° + (percentage * 180° / 100)
  int battery_degrees = 270 + (battery_percent * 180) / 100;
  if (battery_degrees >= 360) {
    battery_degrees -= 360;
  }
  int32_t battery_end = DEG_TO_TRIGANGLE(battery_degrees);
  
  // Draw battery indicator on top of black background
  // Need to split if crossing 0° (when battery > 50%)
  graphics_context_set_fill_color(ctx, battery_color);
  if (battery_percent > 50) {
    // Draw from left to top (270° to 360°/0°)
    graphics_fill_radial(ctx, battery_box, GOvalScaleModeFitCircle, BATTERY_RING_WIDTH, 
                        battery_start, TRIG_MAX_ANGLE);
    // Draw from top to end position (0° to battery_end)
    graphics_fill_radial(ctx, battery_box, GOvalScaleModeFitCircle, BATTERY_RING_WIDTH, 
                        0, battery_end);
  } else {
    // Draw single arc from left to battery position
    graphics_fill_radial(ctx, battery_box, GOvalScaleModeFitCircle, BATTERY_RING_WIDTH, 
                        battery_start, battery_end);
  }
}

// Draw twilight shadows
static void draw_twilight_shadows(GContext *ctx) {
  if (!s_twilight.valid) {
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Twilight data not valid, skipping shadows");
    return;
  }
  
  // Create bounding box for radial fills - needs to be centered
  GRect box = GRect(s_center.x - s_radius, s_center.y - s_radius, 
                    s_radius * 2, s_radius * 2);
  
  // Draw from inside out, so later layers don't cover earlier ones
  // Start with night as the base (full circle)
  graphics_context_set_fill_color(ctx, COLOR_NIGHT);
  graphics_fill_radial(ctx, box, GOvalScaleModeFitCircle, TWILIGHT_RING_WIDTH, 0, TRIG_MAX_ANGLE);
  
  // Astronomical twilight
  int32_t astro_begin = minutes_to_angle(s_twilight.astronomical_twilight_begin);
  int32_t astro_end = minutes_to_angle(s_twilight.astronomical_twilight_end);
  graphics_context_set_fill_color(ctx, COLOR_ASTRONOMICAL_TWILIGHT);
  graphics_fill_radial(ctx, box, GOvalScaleModeFitCircle, TWILIGHT_RING_WIDTH, astro_begin, TRIG_MAX_ANGLE);
  graphics_fill_radial(ctx, box, GOvalScaleModeFitCircle, TWILIGHT_RING_WIDTH, 0, astro_end);
  
  // Nautical twilight
  int32_t naut_begin = minutes_to_angle(s_twilight.nautical_twilight_begin);
  int32_t naut_end = minutes_to_angle(s_twilight.nautical_twilight_end);
  graphics_context_set_fill_color(ctx, COLOR_NAUTICAL_TWILIGHT);
  graphics_fill_radial(ctx, box, GOvalScaleModeFitCircle, TWILIGHT_RING_WIDTH, naut_begin, TRIG_MAX_ANGLE);
  graphics_fill_radial(ctx, box, GOvalScaleModeFitCircle, TWILIGHT_RING_WIDTH, 0, naut_end);
  
  // Civil twilight
  int32_t civil_begin = minutes_to_angle(s_twilight.civil_twilight_begin);
  int32_t civil_end = minutes_to_angle(s_twilight.civil_twilight_end);
  graphics_context_set_fill_color(ctx, COLOR_CIVIL_TWILIGHT);
  graphics_fill_radial(ctx, box, GOvalScaleModeFitCircle, TWILIGHT_RING_WIDTH, civil_begin, TRIG_MAX_ANGLE);
  graphics_fill_radial(ctx, box, GOvalScaleModeFitCircle, TWILIGHT_RING_WIDTH, 0, civil_end);
  
  // Day
  int32_t sunrise = minutes_to_angle(s_twilight.sunrise);
  int32_t sunset = minutes_to_angle(s_twilight.sunset);
  graphics_context_set_fill_color(ctx, COLOR_DAY);
  graphics_fill_radial(ctx, box, GOvalScaleModeFitCircle, TWILIGHT_RING_WIDTH, sunrise, TRIG_MAX_ANGLE);
  graphics_fill_radial(ctx, box, GOvalScaleModeFitCircle, TWILIGHT_RING_WIDTH, 0, sunset);
}

// Draw hour marks
static void draw_hour_marks(GContext *ctx) {
  graphics_context_set_stroke_color(ctx, COLOR_MARKS);
  graphics_context_set_stroke_width(ctx, 2);
  
  for (int i = 0; i < 24; i++) {
    int32_t angle = (i * TRIG_MAX_ANGLE) / 24;
    
    bool is_major = (i % 6 == 0);
    
    if (s_show_hour_numbers && is_major) {
      char *text = "";
      
      switch (i) {
        case 0: // Noon (12)
          text = "12";
          break;
        case 6: // 18h
          text = "18";
          break;
        case 12: // Midnight (0)
          text = "0";
          break;
        case 18: // 6h
          text = "6";
          break;
      }
      
      // Calculate position
      // Position needs to be further in than the ticks to fit
      int16_t dist = s_radius - 12; // Adjust distance
      GPoint pos = {
        .x = s_center.x + (sin_lookup(angle) * dist / TRIG_MAX_RATIO),
        .y = s_center.y + (-cos_lookup(angle) * dist / TRIG_MAX_RATIO)
      };
      
      GRect rect = GRect(pos.x - 10, pos.y - 10, 20, 20); // 20x20 box
      
      // Draw Shadow (Black) at offset +1,+1
      GRect shadow_rect = GRect(rect.origin.x + 1, rect.origin.y + 1, rect.size.w, rect.size.h);
      graphics_context_set_text_color(ctx, GColorBlack);
      graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), shadow_rect, 
                         GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

      // Draw second shadow (Black) at offset -1,-1
      shadow_rect = GRect(rect.origin.x - 1, rect.origin.y - 1, rect.size.w, rect.size.h);
      graphics_context_set_text_color(ctx, GColorBlack);
      graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), shadow_rect, 
                         GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

      // Draw Text (White)
      graphics_context_set_text_color(ctx, GColorWhite);
      graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), rect, 
                         GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
                         
    } else {
      // Draw tick as before
      
      int16_t inner_radius = (is_major) ? s_radius - 15 : s_radius - 7;
      
      GPoint outer = {
        .x = s_center.x + (sin_lookup(angle) * s_radius / TRIG_MAX_RATIO),
        .y = s_center.y + (-cos_lookup(angle) * s_radius / TRIG_MAX_RATIO)
      };
      
      GPoint inner = {
        .x = s_center.x + (sin_lookup(angle) * inner_radius / TRIG_MAX_RATIO),
        .y = s_center.y + (-cos_lookup(angle) * inner_radius / TRIG_MAX_RATIO)
      };
      
      graphics_draw_line(ctx, outer, inner);
    }
  }
}

// Draw a hand (or segment)
static void draw_hand(GContext *ctx, int32_t angle, int16_t inner_radius, int16_t outer_radius, int16_t width, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, width);
  
  GPoint start = {
    .x = s_center.x + (sin_lookup(angle) * inner_radius / TRIG_MAX_RATIO),
    .y = s_center.y + (-cos_lookup(angle) * inner_radius / TRIG_MAX_RATIO)
  };
  
  GPoint end = {
    .x = s_center.x + (sin_lookup(angle) * outer_radius / TRIG_MAX_RATIO),
    .y = s_center.y + (-cos_lookup(angle) * outer_radius / TRIG_MAX_RATIO)
  };
  
  graphics_draw_line(ctx, start, end);
}

// Canvas layer update procedure
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  // Get current time
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  
  // Clear background
  graphics_context_set_fill_color(ctx, COLOR_BACKGROUND);
  graphics_fill_rect(ctx, s_bounds, 0, GCornerNone);
  
  // Draw twilight shadows (outer ring - 20 pixels)
  draw_twilight_shadows(ctx);
  
  // Draw 1-pixel black separator border between twilight and battery rings
  GRect box = GRect(s_center.x - s_radius, s_center.y - s_radius, 
                    s_radius * 2, s_radius * 2);
  graphics_context_set_fill_color(ctx, COLOR_SEPARATOR);
  graphics_fill_radial(ctx, box, GOvalScaleModeFitCircle, SEPARATOR_WIDTH, 0, TRIG_MAX_ANGLE);

  
  // Draw second 1-pixel black separator border on the inner edge of battery ring
  int16_t inner_battery_radius = s_radius - 20; // 20px twilight + 1px border + 10px battery
  GRect inner_box = GRect(s_center.x - inner_battery_radius, s_center.y - inner_battery_radius, 
                          inner_battery_radius * 2, inner_battery_radius * 2);
  graphics_context_set_fill_color(ctx, COLOR_SEPARATOR);
  graphics_fill_radial(ctx, inner_box, GOvalScaleModeFitCircle, SEPARATOR_WIDTH + BATTERY_RING_WIDTH + SEPARATOR_WIDTH, 0, TRIG_MAX_ANGLE);

  // Draw battery indicator (inner ring - 10 pixels)
  draw_battery_indicator(ctx);
  
  // Draw separator between battery and step tracker
  int16_t step_separator_radius = s_radius - TWILIGHT_RING_WIDTH - SEPARATOR_WIDTH - BATTERY_RING_WIDTH - SEPARATOR_WIDTH;
  GRect step_sep_box = GRect(s_center.x - step_separator_radius, s_center.y - step_separator_radius,
                            step_separator_radius * 2, step_separator_radius * 2);
  graphics_context_set_fill_color(ctx, COLOR_SEPARATOR);
  graphics_fill_radial(ctx, step_sep_box, GOvalScaleModeFitCircle, SEPARATOR_WIDTH, 0, TRIG_MAX_ANGLE);

  // Draw step tracker
  draw_step_tracker(ctx);

  // Draw Icons
  // Inner ring edge is at s_radius - 20 (twilight) - 1 (sep) - 10 (battery/step) = s_radius - 31
  // We want to position icons just inside this ring.
  // Let's place them at s_radius - 31 - padding (e.g. 2px) - half_icon_height
  // Using a fixed offset from center for simplicity: s_radius - 42 seems safe to start.
  
  if (s_battery_icon_bitmap) {
    // Console Log
    GRect bounds = gbitmap_get_bounds(s_battery_icon_bitmap);
    GSize size = bounds.size;
    int16_t icon_offset = s_radius - 42;
    // Battery at top (12 o'clock)
    GRect target = GRect(s_center.x - size.w / 2, s_center.y - icon_offset - size.h / 2, size.w, size.h);
    
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, s_battery_icon_bitmap, target);
  }

  if (s_steps_icon_bitmap && s_step_goal > 0) {
    GRect bounds = gbitmap_get_bounds(s_steps_icon_bitmap);
    GSize size = bounds.size;
    int16_t icon_offset = s_radius - 42;
    // Steps at bottom (6 o'clock)
    GRect target = GRect(s_center.x - size.w / 2, s_center.y + icon_offset - size.h / 2, size.w, size.h);
    
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, s_steps_icon_bitmap, target);
  }
  
  // Reset compositing mode
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
  
  // Draw hour marks
  draw_hour_marks(ctx);
  
  // Calculate hand angles

  // Minute hand: 60 minute rotation, with 0 minutes at top
  int32_t minute_angle = (t->tm_min * TRIG_MAX_ANGLE) / 60;
  
  // Draw hands (minute hand first, so hour hand appears on top)
  
  // Minute hand color logic
  GColor minute_hand_color;
  #ifdef PBL_COLOR
    minute_hand_color = COLOR_MINUTE_HAND;
  #else
    // For B/W: check contrast against the ring background
    // Calculate what time corresponds to the minute hand's angle on the 24h ring
    // 0 deg = Noon (720 min), 180 deg = Midnight (1440/0 min)
    // Formula inverse of minutes_to_angle:
    // angle = (minutes - 720) * ...
    // minutes = (angle * 1440 / MAX_ANGLE) + 720
    int ring_minutes = ((minute_angle * 1440) / TRIG_MAX_ANGLE + 720);
    if (ring_minutes >= 1440) ring_minutes -= 1440;
    
    PeriodType min_period = get_current_period(ring_minutes);
    if (min_period == PERIOD_NIGHT) {
      minute_hand_color = COLOR_MINUTE_HAND_OVER_NIGHT;
    } else {
      minute_hand_color = COLOR_MINUTE_HAND_OVER_DAY;
    }
  #endif

  // Minute hand: Only outer half of outermost ring (Length 10, width 3)
  draw_hand(ctx, minute_angle, s_radius - 20, s_radius - 10, 3, minute_hand_color);

  // Hour hand: 24 hour rotation with noon (12:00) at top
  // Convert current time to minutes since midnight, then offset by noon (720 minutes)
  int current_minutes = t->tm_hour * 60 + t->tm_min;
  int32_t hour_angle = ((current_minutes - 720) * TRIG_MAX_ANGLE) / 1440;
  if (current_minutes < 720) {
    hour_angle += TRIG_MAX_ANGLE; // Wrap around for morning hours
  }
  
  // Hour hand color logic
  GColor hour_hand_color;
  #ifdef PBL_COLOR
    hour_hand_color = COLOR_HOUR_HAND;
  #else
    // For B/W: check if we are in night or day/twilight
    PeriodType period = get_current_period(current_minutes);
    if (period == PERIOD_NIGHT) {
      hour_hand_color = COLOR_HOUR_HAND_OVER_NIGHT; // White on Black (Night)
    } else {
      hour_hand_color = COLOR_HOUR_HAND_OVER_DAY; // Black on White/Gray (Day/Twilight)
    }
  #endif
  
  // Hour hand: ONLY over outer ring (radius-20 to radius)
  draw_hand(ctx, hour_angle, s_radius - 20, s_radius, 5, hour_hand_color);
  
  // Draw center dot
  // graphics_context_set_fill_color(ctx, COLOR_MINUTE_HAND); // Use minute hand color for dot
  // graphics_fill_circle(ctx, s_center, 2);
}

// Time tick handler
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  // Update date if day changed
  if (units_changed & DAY_UNIT) {
    update_date_display();
  }
  
  // Redraw
  if (s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }
}

// Send timezone to JS
static void send_timezone_to_js() {
  char timezone_name[TIMEZONE_NAME_LENGTH];
  clock_get_timezone(timezone_name, TIMEZONE_NAME_LENGTH);
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Sending timezone to JS: %s", timezone_name);
  
  DictionaryIterator *out_iter;
  AppMessageResult result = app_message_outbox_begin(&out_iter);
  if (result == APP_MSG_OK) {
    dict_write_cstring(out_iter, MESSAGE_KEY_timezone_string, timezone_name);
    
    // Also send step goal request/confirm if valid?
    // Actually the JS handles logic.
    app_message_outbox_send();
  } else {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Error preparing outbox: %d", (int)result);
  }
}

// AppMessage handlers
static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Message received from phone");

  // Check if JS is ready
  Tuple *js_ready_tuple = dict_find(iter, MESSAGE_KEY_js_ready);
  if (js_ready_tuple) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "JS is ready, sending timezone");
    send_timezone_to_js();
    return;
  }
  
  // Read date configuration
  Tuple *date_format_tuple = dict_find(iter, MESSAGE_KEY_date_format_us);
  Tuple *show_day_tuple = dict_find(iter, MESSAGE_KEY_show_day_of_week);
  
  bool config_changed = false;
  if (date_format_tuple) {
    s_date_config.date_format_us = date_format_tuple->value->int32 == 1;
    config_changed = true;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Date format US: %d", s_date_config.date_format_us);
  }
  
  if (show_day_tuple) {
    s_date_config.show_day_of_week = show_day_tuple->value->int32 == 1;
    config_changed = true;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Show day of week: %d", s_date_config.show_day_of_week);
  }
  
  // Read step goal
  Tuple *step_goal_tuple = dict_find(iter, MESSAGE_KEY_step_goal);
  if (step_goal_tuple) {
    s_step_goal = (int)step_goal_tuple->value->int32;
    persist_write_int(STORAGE_KEY_STEP_GOAL, s_step_goal);
    get_step_count(); // Update steps with new goal (enable/disable check)
    if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Step goal updated: %d", s_step_goal);
  }

  // Read show hour numbers
  Tuple *hour_numbers_tuple = dict_find(iter, MESSAGE_KEY_show_hour_numbers);
  if (hour_numbers_tuple) {
    s_show_hour_numbers = (hour_numbers_tuple->value->int32 == 1);
    persist_write_bool(STORAGE_KEY_SHOW_HOUR_NUMBERS, s_show_hour_numbers);
    if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Show Hour Numbers: %d", s_show_hour_numbers);
  }
  
  if (config_changed) {
    persist_write_data(STORAGE_KEY_DATE_CONFIG, &s_date_config, sizeof(DateConfig));
    update_date_display();
  }
  
  // Read twilight data
  Tuple *sunrise_tuple = dict_find(iter, MESSAGE_KEY_sunrise);
  Tuple *sunset_tuple = dict_find(iter, MESSAGE_KEY_sunset);
  Tuple *civil_begin_tuple = dict_find(iter, MESSAGE_KEY_civil_twilight_begin);
  Tuple *civil_end_tuple = dict_find(iter, MESSAGE_KEY_civil_twilight_end);
  Tuple *nautical_begin_tuple = dict_find(iter, MESSAGE_KEY_nautical_twilight_begin);
  Tuple *nautical_end_tuple = dict_find(iter, MESSAGE_KEY_nautical_twilight_end);
  Tuple *astro_begin_tuple = dict_find(iter, MESSAGE_KEY_astronomical_twilight_begin);
  Tuple *astro_end_tuple = dict_find(iter, MESSAGE_KEY_astronomical_twilight_end);
  
  if (sunrise_tuple && sunset_tuple) {
    s_twilight.sunrise = (int16_t)sunrise_tuple->value->int32;
    s_twilight.sunset = (int16_t)sunset_tuple->value->int32;
    s_twilight.civil_twilight_begin = (int16_t)civil_begin_tuple->value->int32;
    s_twilight.civil_twilight_end = (int16_t)civil_end_tuple->value->int32;
    s_twilight.nautical_twilight_begin = (int16_t)nautical_begin_tuple->value->int32;
    s_twilight.nautical_twilight_end = (int16_t)nautical_end_tuple->value->int32;
    s_twilight.astronomical_twilight_begin = (int16_t)astro_begin_tuple->value->int32;
    s_twilight.astronomical_twilight_end = (int16_t)astro_end_tuple->value->int32;
    s_twilight.valid = true;
    
    // Save to persistent storage
    persist_write_data(STORAGE_KEY_TWILIGHT, &s_twilight, sizeof(TwilightData));
    
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Twilight data updated: sunrise=%d, sunset=%d", 
            s_twilight.sunrise, s_twilight.sunset);
    
    // Redraw
    if (s_canvas_layer) {
      layer_mark_dirty(s_canvas_layer);
    }
  }
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped: %d", reason);
}

static void outbox_failed_handler(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed: %d", reason);
}

static void outbox_sent_handler(DictionaryIterator *iter, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Outbox send success!");
}

// Window load/unload
static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  s_bounds = layer_get_bounds(window_layer);
  
  // Calculate display properties
  s_center = grect_center_point(&s_bounds);
  
#ifdef PBL_ROUND
  s_is_round = true;
  s_radius = s_bounds.size.w / 2 - 5;
#else
  s_is_round = false;
  // Use smaller dimension for circular clock face
  int16_t min_dim = (s_bounds.size.w < s_bounds.size.h) ? s_bounds.size.w : s_bounds.size.h;
  s_radius = min_dim / 2 - 5;
#endif
  
  // Create canvas layer
  s_canvas_layer = layer_create(s_bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);
  
  // Create date layer at 30% from bottom of circle
  // Position: center.y + (radius * 0.3)
  int16_t date_y = s_center.y; // + (s_radius * 30 / 100);
  s_date_layer = text_layer_create(GRect(0, date_y - 7, s_bounds.size.w, 20));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, GColorWhite);
  text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_date_layer));
  
  // Initialize date display
  update_date_display();
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Window loaded: center=(%d,%d), radius=%d, round=%d", 
          s_center.x, s_center.y, s_radius, s_is_round);

  // Load resources
  s_battery_icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BATTERY);
  s_steps_icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_STEPS);
}

static void window_unload(Window *window) {
  text_layer_destroy(s_date_layer);
  layer_destroy(s_canvas_layer);
  s_canvas_layer = NULL;
  s_date_layer = NULL;

  if (s_battery_icon_bitmap) {
    gbitmap_destroy(s_battery_icon_bitmap);
    s_battery_icon_bitmap = NULL;
  }
  if (s_steps_icon_bitmap) {
    gbitmap_destroy(s_steps_icon_bitmap);
    s_steps_icon_bitmap = NULL;
  }
}

// App initialization
static void init(void) {
  setlocale(LC_ALL, "");

  // Load date configuration with defaults
  s_date_config.date_format_us = false;
  s_date_config.show_day_of_week = true;
  
  if (persist_exists(STORAGE_KEY_DATE_CONFIG)) {
    persist_read_data(STORAGE_KEY_DATE_CONFIG, &s_date_config, sizeof(DateConfig));
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Loaded date config: US=%d, ShowDay=%d",
            s_date_config.date_format_us, s_date_config.show_day_of_week);
  }
  
  // Load twilight data from persistent storage
  s_twilight.valid = false;
  if (persist_exists(STORAGE_KEY_TWILIGHT)) {
    persist_read_data(STORAGE_KEY_TWILIGHT, &s_twilight, sizeof(TwilightData));
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Loaded twilight data from storage");
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Loaded twilight data from storage");
  }
  
  // Load step goal
  if (persist_exists(STORAGE_KEY_STEP_GOAL)) {
    s_step_goal = persist_read_int(STORAGE_KEY_STEP_GOAL);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Loaded step goal: %d", s_step_goal);
  }

  // Load show_hour_numbers
  if (persist_exists(STORAGE_KEY_SHOW_HOUR_NUMBERS)) {
    s_show_hour_numbers = persist_read_bool(STORAGE_KEY_SHOW_HOUR_NUMBERS);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Loaded show_hour_numbers: %d", s_show_hour_numbers);
  }
  
  // Subscribe to health events
  if (health_service_events_subscribe(health_handler, NULL)) {
    // Force initial update
    get_step_count();
  } else {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Health not available!");
  }
  
  // Register AppMessage handlers
  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  app_message_register_outbox_failed(outbox_failed_handler);
  app_message_register_outbox_sent(outbox_sent_handler);
  
  // Open AppMessage
  app_message_open(256, 256);
  
  // Create main window
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_set_background_color(s_window, COLOR_BACKGROUND);
  window_stack_push(s_window, true);
  
  // Subscribe to time tick service (minute and day updates)
  tick_timer_service_subscribe(MINUTE_UNIT | DAY_UNIT, tick_handler);
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Sundrive initialized");
}

// App deinitialization
static void deinit(void) {
  tick_timer_service_unsubscribe();
  health_service_events_unsubscribe();
  window_destroy(s_window);
}

// Main entry point
int main(void) {
  init();
  app_event_loop();
  deinit();
}
