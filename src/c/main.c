#include <pebble.h>
const bool devMode = false;
const bool debugMode = false;
const bool devRefreshFast = false;

const int MIN = 60;
const int HOUR = (60*60);

const int PULSE_INTERVAL_SEC = 30;
const int MESSAGE_TIMEOUT = 300000;
const int CLOSE_DISPLAY_TIMEOUT = 1000;
const int SHOW_CLOCK_TIMEOUT = 5000;
const int AUTO_REFRESH_TIMEOUT = 1000*(60*5);

const int LOGS_MAX_SIZE = 200;
const char LOGS_DELIMETER[] = "\n";
const int LOGS_DISPLAY_LINES = 10;
/////////////////////////////////////////

const int JS_GET_STATS = 1;
const int JS_SEND_PULSE = 2;
const int JS_GET_MAP = 3;
const int JS_GET_NEXT_MAP = 4;
const int JS_STATUS_READY = 0;
const int JS_STATUS_STATS_OK = 1;
const int JS_STATUS_PULSE_OK = 2;
const int JS_STATUS_MAP_OK = 3;
const int JS_STATUS_NEXT_MAP_OK = 4;
const int PERSIST_KEY_LOGS = 0;

static Window *s_main_window;
static TextLayer *s_notice_layer;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static TextLayer *s_total_count_layer;
static TextLayer *s_today_count_layer;
static WakeupId s_wakeup_id = 1;
static bool fromWakeUp = false;
static bool autoPulseSuccess = false;
static BitmapLayer *mainBgImgLyr;
static GBitmap *mainBgImg;
static AppTimer *no_response_timer;
static AppTimer *close_display_timer;
static AppTimer *auto_refresh_timer;
static AppTimer *show_clock_timer;
static Layer *s_canvas_layer;

char mapData[1000]="";
char debugStr[256]="";


static void saveLogLine(char *msg) {
  char memData[LOGS_MAX_SIZE];
  persist_read_string(PERSIST_KEY_LOGS, memData, sizeof(memData));
  APP_LOG(APP_LOG_LEVEL_DEBUG, "PERSIST KEY LOGS ...................................\n%d", strlen(memData));
  
  //memData[4] = '\0';
  int exceed = strlen(memData)+strlen(msg)+strlen(LOGS_DELIMETER)+1 - (unsigned)LOGS_MAX_SIZE;
  if( exceed >0) {
    memmove(memData, memData + exceed, strlen(memData));
  }
  strcat(memData, msg);
  strcat(memData, LOGS_DELIMETER);
  
  // Write the string
  APP_LOG(APP_LOG_LEVEL_DEBUG, "MSG ...................................\n%s", msg);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "PERSIST KEY LOGS ...................................\n%s", memData);
  
  persist_write_string(PERSIST_KEY_LOGS, memData);
}

static void appendLog(char *msg) {
  if(fromWakeUp) {   
    // Get a tm structure
    time_t temp = time(NULL);
    struct tm *tick_time = localtime(&temp);
  
    // Write the current hours and minutes into a buffer
    char s_buffer[50];
    strftime(s_buffer, sizeof(s_buffer), "%m/%d %H.%M", tick_time);
    //strftime(s_buffer, sizeof(s_buffer), "%m/%d %H.%M.%S", tick_time);
    strcat(s_buffer, ": ");
    strcat(s_buffer, msg);
    
    saveLogLine(s_buffer);
  } 
}

static void debugOut( char *msg ) {
//   if(debugMode) {
//     strcat( debugStr, msg);
//     strcat( debugStr, ">");
    
//     layer_set_hidden(text_layer_get_layer(s_notice_layer), false);
//     text_layer_set_text(s_notice_layer, debugStr); 
//   }
}

static char* codeToString( bool isStatusCode, int code ) {
  if(isStatusCode) {
    if(code==0)
      return "JS_STATUS_READY";
    else if(code==1)
      return "JS_STATUS_STATS_OK";
    else if(code==2)
      return "JS_STATUS_PULSE_OK";
    else if(code==3)
      return "JS_STATUS_MAP_OK";
    else if(code==4)
      return "JS_STATUS_NEXT_MAP_OK";
  } else {
    if(code==1)
      return "JS_GET_STATS";
    else if(code==2)
      return "JS_SEND_PULSE";
    else if(code==3)
      return "JS_GET_MAP";
    else if(code==4)
      return "JS_GET_NEXT_MAP";
  }
  return "";
}
  

static void endApp(){
  if(fromWakeUp && !autoPulseSuccess) {    
    time_t wakeup_time = time(NULL) + HOUR;
    wakeup_schedule(wakeup_time, 100, true);
  }
  window_stack_pop(false);
}

static void close_display_timer_callback(void *context){  
  if(fromWakeUp)
    endApp();
  else      
    layer_set_hidden(text_layer_get_layer(s_notice_layer), true);
}

static void delayCloseDisplay() {
  if(debugMode) {
    vibes_long_pulse();
  } else {
    close_display_timer = app_timer_register( CLOSE_DISPLAY_TIMEOUT, close_display_timer_callback, NULL);
  }
}

static void no_response_timer_callback(void *context){  
    
  if(connection_service_peek_pebble_app_connection()) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "no response...................................");
    layer_set_hidden(text_layer_get_layer(s_notice_layer), false);
    text_layer_set_text(s_notice_layer, "\n\n\n\n\nPiA: Connection Timeout!");
    appendLog("Con Timeout");
    delayCloseDisplay();
  } else {    
    APP_LOG(APP_LOG_LEVEL_ERROR, "..................bluetooth not connected");
    text_layer_set_text(s_notice_layer, "\n\n\n\n\nPiA: Can't connect to phone!");
    layer_set_hidden(text_layer_get_layer(s_notice_layer), false);
    appendLog("BT discon");
    delayCloseDisplay();
  }
}

static void sendMessage(int code){
  APP_LOG(APP_LOG_LEVEL_DEBUG, "PBL -------------> PHONE : %s", codeToString(false, code));
  no_response_timer = app_timer_register( MESSAGE_TIMEOUT, no_response_timer_callback, NULL);
  
  // Begin dictionary
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);

  // Add a key-value pair
  dict_write_int(iter, MESSAGE_KEY_CODE, &code, sizeof(int), true);

  // Send the message!
  app_message_outbox_send();
}

static void auto_refresh_timer_callback(void *context){  
  sendMessage(JS_GET_STATS);
}

bool startsWith(const char *pre, const char *str)
{
    size_t lenpre = strlen(pre),
           lenstr = strlen(str);
    return lenstr < lenpre ? false : strncmp(pre, str, lenpre) == 0;
}

int B64toD(char b64char){
  if(b64char>='A' && b64char<='Z')
    return b64char-'A';
  else if(b64char>='a' && b64char<='z')
    return b64char-'a'+26;
  else if(b64char>='0' && b64char<='9')
    return b64char-'0'+52;
  else if(b64char=='+')
    return 62;
  else if(b64char=='/')
    return 63;
  else
    return 0;
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  
  int i=0;
  for(char* it = mapData; *it; it+=3) {
    int locNum = (B64toD(*it) *64*64) +(B64toD(*(it+1)) *64) +B64toD(*(it+2));
    int posLat= (locNum/1000);
    int posLong= (locNum%1000);
    //APP_LOG(APP_LOG_LEVEL_ERROR, "-------> %d x %d", posLat, posLong);
  
    if(posLat<210) {
      int origX=PBL_IF_ROUND_ELSE(0,-18)+(posLong*144)/315;
      int origY=(113+PBL_IF_ROUND_ELSE(18,12))-((posLat*113)/240);
      //APP_LOG(APP_LOG_LEVEL_ERROR, "new -------> %d x %d", origX, origY);
      
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_fill_circle(ctx, GPoint(origX, origY),3);
      graphics_draw_circle(ctx, GPoint(origX, origY),2);
    }
    i++;
  }
  APP_LOG(APP_LOG_LEVEL_INFO, "MAP TOTAL POINTS: %d", i);
  
  /*
  int origX, origY;
  
    origX=(360*144)/360;
    origY=(110+15)-((210*110)/240);
    //APP_LOG(APP_LOG_LEVEL_ERROR, "new -------> %d x %d", origX, origY);
    
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, GPoint(origX, origY),3);
    graphics_draw_circle(ctx, GPoint(origX, origY),2);
   
    origX=(0*144)/360;
    origY=(110+15)-((210*110)/240);
    //APP_LOG(APP_LOG_LEVEL_ERROR, "new -------> %d x %d", origX, origY);
    
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, GPoint(origX, origY),3);
    graphics_draw_circle(ctx, GPoint(origX, origY),2);
   
    origX=(0*144)/360;
    origY=(110+15)-((60*110)/240);
    //APP_LOG(APP_LOG_LEVEL_ERROR, "new -------> %d x %d", origX, origY);
    
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, GPoint(origX, origY),3);
    graphics_draw_circle(ctx, GPoint(origX, origY),2);
   
    origX=(360*144)/360;
    origY=(110+15)-((60*110)/240);
    //APP_LOG(APP_LOG_LEVEL_ERROR, "new -------> %d x %d", origX, origY);
    
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, GPoint(origX, origY),3);
    graphics_draw_circle(ctx, GPoint(origX, origY),2);
   */
}


static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  if(no_response_timer)
    app_timer_cancel(no_response_timer);
  
  // Store incoming information
  static int status;
 
  // Read tuples for data
  Tuple *status_tuple = dict_find(iterator, MESSAGE_KEY_STATUS);
  // If all data is available, use it
  if(status_tuple) {
    status = (int)status_tuple->value->int32;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "PBL <------------- PHONE : %s", codeToString(true, status));
    if (status==JS_STATUS_READY) {
      if(fromWakeUp) {
        appendLog("rdy pulsin");
        sendMessage(JS_SEND_PULSE);
      } else {
        appendLog("rdy stats");
        sendMessage(JS_GET_STATS);
      }
        
    } else if(status==JS_STATUS_STATS_OK) {
      
      Tuple *data1_tuple = dict_find(iterator, MESSAGE_KEY_DATA1);
      Tuple *data2_tuple = dict_find(iterator, MESSAGE_KEY_DATA2);
      static char data1_buffer[10];
      static char data2_buffer[64];
      snprintf(data1_buffer, sizeof(data1_buffer), "%s", data1_tuple->value->cstring);
      snprintf(data2_buffer, sizeof(data2_buffer), "%s", data2_tuple->value->cstring);
      
      
      if(strcmp(data1_buffer, "nok")==0){
        APP_LOG(APP_LOG_LEVEL_ERROR, "NOK!");
        appendLog("Conn Not OK");
        layer_set_hidden(text_layer_get_layer(s_notice_layer), false);
        text_layer_set_text(s_notice_layer, data2_buffer);
        delayCloseDisplay(); 
      } else {
        appendLog("get map");
        text_layer_set_text(s_today_count_layer, data1_buffer);
        text_layer_set_text(s_total_count_layer, data2_buffer);
        sendMessage(JS_GET_MAP);
      }
        
      if(auto_refresh_timer)
       app_timer_cancel(auto_refresh_timer);
      
      auto_refresh_timer = app_timer_register( AUTO_REFRESH_TIMEOUT, auto_refresh_timer_callback, NULL);
    } else if (status==JS_STATUS_PULSE_OK) {
      
      Tuple *data1_tuple = dict_find(iterator, MESSAGE_KEY_DATA1);
      static char data1_buffer[32];
      snprintf(data1_buffer, sizeof(data1_buffer), "%s", data1_tuple->value->cstring);
      
      
      layer_set_hidden(text_layer_get_layer(s_notice_layer), false);
      if(strcmp(data1_buffer, "ok\n")){
        appendLog("Pulse done");
        debugOut("conPulsOk");
        text_layer_set_text(s_notice_layer, "\n\n\n\n\nPiA: Pulse Sent!");
      }else{
        appendLog("Pulse Fail");
        debugOut(data1_buffer);
        text_layer_set_text(s_notice_layer, data1_buffer);
      }
      delayCloseDisplay(); 
    } else if(status==JS_STATUS_MAP_OK) {
        appendLog("map ok");
        strcpy(mapData, "");
        sendMessage(JS_GET_NEXT_MAP);
    } else if(status==JS_STATUS_NEXT_MAP_OK) {
        
        Tuple *data1_tuple = dict_find(iterator, MESSAGE_KEY_DATA1);
        static char data1_buffer[150];
        snprintf(data1_buffer, sizeof(data1_buffer), "%s", data1_tuple->value->cstring);
        
        if(strcmp(data1_buffer, "nok")==0){
          APP_LOG(APP_LOG_LEVEL_ERROR, "MAP NOK!");
        } else if(strcmp(data1_buffer, "@END")==0){
          APP_LOG(APP_LOG_LEVEL_INFO, "No more map chunk");  
          layer_mark_dirty(s_canvas_layer);      
        } else {
          /////////////////////////////////// MAP DATA IN HERE
          APP_LOG(APP_LOG_LEVEL_DEBUG, "MAP CHUNK: %s", data1_tuple->value->cstring);
          //snprintf(mapData, sizeof(mapData), "%s", data1_tuple->value->cstring);   
          strcat(mapData, data1_tuple->value->cstring);
          APP_LOG(APP_LOG_LEVEL_DEBUG, "MAP DATA: %s", mapData);
          
          sendMessage(JS_GET_NEXT_MAP);    
        }
    }
  } else {
    appendLog("msg unknown");
  }
  
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  if(no_response_timer)
    app_timer_cancel(no_response_timer);
  layer_set_hidden(text_layer_get_layer(s_notice_layer), false);
  text_layer_set_text(s_notice_layer, "\n\n\n\n\nFailed! can't connect.");
  delayCloseDisplay();
  appendLog("Msg drop");
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped!");
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  if(no_response_timer)
    app_timer_cancel(no_response_timer);
  appendLog("Outbox fail");
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed!");
  text_layer_set_text(s_notice_layer, "\n\n\n\n\nFailed!");
  layer_set_hidden(text_layer_get_layer(s_notice_layer), false);
  delayCloseDisplay();
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
}

////////////////////////////////////////////////////////////////////////////////////

static void update_time_min() {
  // Get a tm structure
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  // Write the current hours and minutes into a buffer
  static char s_buffer[10];
  strftime(s_buffer, sizeof(s_buffer), "%l:%M %p", tick_time);

  APP_LOG(APP_LOG_LEVEL_INFO, "TIMER TICK! %s", s_buffer);
  // Display this time on the TextLayer
  text_layer_set_text(s_time_layer, s_buffer);
  
  
  // Write the current month and day into a buffer
  static char s_buffer2[20];
  strftime(s_buffer2, sizeof(s_buffer2), PBL_IF_ROUND_ELSE("%b%e, %Y (%a)","%b %e"), tick_time);

  // Display this time on the TextLayer
  text_layer_set_text(s_date_layer, s_buffer2);
}

static void tick_handler_min(struct tm *tick_time, TimeUnits units_changed) {
  update_time_min();
}

static void show_clock_timer_callback(void *context){  
  layer_set_hidden(text_layer_get_layer(s_time_layer), false);
  layer_set_hidden(text_layer_get_layer(s_date_layer), false);
}


static void wakeUpInit(){
  //time_t wakeup_time = time(NULL) + PULSE_INTERVAL_SEC;
  if( devMode && devRefreshFast ) {
    time_t wakeup_time = time(NULL) + 15;
    wakeup_schedule(wakeup_time, 100, true);
  } else {
    for(int i=0; i<5; i++) {
      time_t wakeup_time = time_start_of_today() + (HOUR*(12 + (i*24) ));
      wakeup_schedule(wakeup_time, (101+i), true);
    }
  }
}


static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  //text_layer_set_text(s_notice_layer, "Sending Pulse!");
  //sendMessage(JS_SEND_PULSE);
  text_layer_set_text(s_today_count_layer, "---");
  sendMessage(JS_GET_STATS);
}
static void up_release_long_handler(ClickRecognizerRef recognizer, void *context) {
  
}
static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  if(layer_get_hidden(text_layer_get_layer(s_notice_layer))==false) {
    layer_set_hidden(text_layer_get_layer(s_notice_layer), true);  
  }
}
static void up_press_long_handler(ClickRecognizerRef recognizer, void *context) {
  if(layer_get_hidden(text_layer_get_layer(s_notice_layer))==false) {
    APP_LOG(APP_LOG_LEVEL_INFO, "---- append log");    
    appendLog("test");
  } //else {
  
  
    //if (persist_exists(PERSIST_KEY_LOGS)) {

      static char displayBuffer[256];
      strcpy(displayBuffer, "View logs:\n");
      char buffer[256]="";
      persist_read_string(PERSIST_KEY_LOGS, buffer, sizeof(buffer));
      int lineCtr=0;
      int displayIndex=strlen(buffer);
      while(displayIndex>0 && lineCtr<LOGS_DISPLAY_LINES) {
        displayIndex--;
        if(buffer[displayIndex-1]=='\n')
          lineCtr++;
      }
      memmove(buffer, buffer + displayIndex, strlen(buffer));
      APP_LOG(APP_LOG_LEVEL_INFO, "---- -------------- lines........%d", displayIndex);
      
      APP_LOG(APP_LOG_LEVEL_INFO, "---- read from persist memory: %s", buffer);
      strcat(displayBuffer, buffer);
      text_layer_set_text(s_notice_layer, displayBuffer);
      layer_set_hidden(text_layer_get_layer(s_notice_layer), false);  
    //} 
 // }
  
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_long_click_subscribe(BUTTON_ID_UP, 500, up_press_long_handler, up_release_long_handler);
}
// static void centerText(TextLayer * text_layer) {
//   APP_LOG(APP_LOG_LEVEL_ERROR, "text_layer get size %d", layer_get_bounds(text_layer_get_layer(text_layer)).size.h);
//   APP_LOG(APP_LOG_LEVEL_ERROR, "text_layer get content size %d", text_layer_get_content_size(text_layer).h);
//   int fullHeight = layer_get_bounds(text_layer_get_layer(text_layer)).size.h;
  
//   char newLines[] = "";
//   char origMessage[sizeof(text_layer_get_text(text_layer))] =  text_layer_get_text(text_layer);
//   strcpy(newMessage, text_layer_get_text(text_layer));
//   strcat(test, ">>");
//   //while(text_layer_get_content_size(text_layer).h < (fullHeight/2)) {
//     //snprintf(text_layer_buffer, sizeof(text_layer_buffer)+20, "%s%s", newLines, text_layer_get_text(text_layer));

//     text_layer_set_text(text_layer, test);
//   strcat(test, ">>");
//   strcat(test, ">>");
//   strcat(test, ">>");
//   text_layer_set_text(text_layer, test);
    
//     APP_LOG(APP_LOG_LEVEL_ERROR, "text_layer get content size %d", text_layer_get_content_size(text_layer).h);
//     APP_LOG(APP_LOG_LEVEL_ERROR, "text_layer: %s", text_layer_get_text(text_layer));
//   //}
// }

static void main_window_load ( Window *window ){     
  
  Layer *window_layer= window_get_root_layer( window );
  GRect bounds = layer_get_bounds(window_layer);
  int screenOffsetX = PBL_IF_ROUND_ELSE(20, 0);
  int screenOffsetY = PBL_IF_ROUND_ELSE(-20, 0);
  
  
  if(!fromWakeUp){
    mainBgImg = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MAIN_BG);
    mainBgImgLyr = bitmap_layer_create(GRect(0 , 0 , bounds.size.w, bounds.size.h));
    bitmap_layer_set_bitmap(mainBgImgLyr, mainBgImg);
    layer_add_child(window_layer, bitmap_layer_get_layer(mainBgImgLyr));
    
    s_total_count_layer = text_layer_create( GRect(PBL_IF_ROUND_ELSE(0,0), PBL_IF_ROUND_ELSE(110, 102), PBL_IF_ROUND_ELSE(82, 144), 30) );
    text_layer_set_text_color(s_total_count_layer, GColorWhite);
    text_layer_set_background_color(s_total_count_layer, GColorClear);
    text_layer_set_text(s_total_count_layer, "---");
    text_layer_set_font(s_total_count_layer,  fonts_get_system_font(PBL_IF_ROUND_ELSE(FONT_KEY_LECO_28_LIGHT_NUMBERS,FONT_KEY_LECO_28_LIGHT_NUMBERS)));
    text_layer_set_text_alignment(s_total_count_layer, GTextAlignmentRight);
    layer_add_child( window_layer, text_layer_get_layer(s_total_count_layer));

    s_today_count_layer = text_layer_create( GRect(PBL_IF_ROUND_ELSE(98,0), PBL_IF_ROUND_ELSE(110, 135), PBL_IF_ROUND_ELSE(82, 144), 30) );
    text_layer_set_text_color(s_today_count_layer, GColorWhite);
    text_layer_set_background_color(s_today_count_layer, GColorClear);
    text_layer_set_text(s_today_count_layer, "---");
    text_layer_set_font(s_today_count_layer,  fonts_get_system_font(PBL_IF_ROUND_ELSE(FONT_KEY_LECO_28_LIGHT_NUMBERS,FONT_KEY_LECO_28_LIGHT_NUMBERS)));
    text_layer_set_text_alignment(s_today_count_layer, PBL_IF_ROUND_ELSE(GTextAlignmentLeft,GTextAlignmentRight));
    layer_add_child( window_layer, text_layer_get_layer(s_today_count_layer));
    
    bool isRound = PBL_IF_ROUND_ELSE(true, false);
    if(isRound) {
      
      s_date_layer = text_layer_create( GRect(0,16, bounds.size.w, 14) );
      text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
      text_layer_set_font(s_date_layer,  fonts_get_system_font(FONT_KEY_GOTHIC_14));
      text_layer_set_text_color(s_date_layer, GColorWhite);
      text_layer_set_background_color(s_date_layer, GColorBlack);
      text_layer_set_text(s_date_layer, "12/25");
      layer_add_child( window_layer, text_layer_get_layer(s_date_layer));
      
      s_time_layer = text_layer_create( GRect(0,0, bounds.size.w, 18) );
      text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
      text_layer_set_font(s_time_layer,  fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
      text_layer_set_text_color(s_time_layer, GColorWhite);
      text_layer_set_background_color(s_time_layer, GColorBlack);
      text_layer_set_text(s_time_layer, "10:25 AM");
      layer_add_child( window_layer, text_layer_get_layer(s_time_layer));
    } else {
      s_time_layer = text_layer_create( GRect(0,0, bounds.size.w/2, 20) );
      text_layer_set_text_alignment(s_time_layer, GTextAlignmentLeft);
      text_layer_set_font(s_time_layer,  fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
      text_layer_set_text_color(s_time_layer, GColorWhite);
      text_layer_set_background_color(s_time_layer, GColorBlack);
      text_layer_set_text(s_time_layer, "10:25 AM");
      layer_add_child( window_layer, text_layer_get_layer(s_time_layer));
      
      s_date_layer = text_layer_create( GRect(bounds.size.w/2,0, bounds.size.w/2, 20) );
      text_layer_set_text_alignment(s_date_layer, GTextAlignmentRight);
      text_layer_set_font(s_date_layer,  fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
      text_layer_set_text_color(s_date_layer, GColorWhite);
      text_layer_set_background_color(s_date_layer, GColorBlack);
      text_layer_set_text(s_date_layer, "12/25");
      layer_add_child( window_layer, text_layer_get_layer(s_date_layer));
    }
    
    update_time_min();
    layer_set_hidden(text_layer_get_layer(s_time_layer), true);
    layer_set_hidden(text_layer_get_layer(s_date_layer), true);
    // Register with TickTimerService
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler_min);
    show_clock_timer = app_timer_register( SHOW_CLOCK_TIMEOUT, show_clock_timer_callback, NULL);  
    
    // Create canvas layer
    s_canvas_layer = layer_create(bounds);
    
    // Assign the custom drawing procedure
    layer_set_update_proc(s_canvas_layer, canvas_update_proc);
    
    // Add to Window
    layer_add_child(window_layer, s_canvas_layer);
  }
  
  s_notice_layer = text_layer_create( GRect(0,0, bounds.size.w, bounds.size.h) );
  text_layer_set_text_color(s_notice_layer, GColorWhite);
  text_layer_set_background_color(s_notice_layer, GColorBlack);
  text_layer_set_text(s_notice_layer, "\n\n\n\n\nPiA: Sending Pulse!");
  //centerText(s_notice_layer);
  text_layer_set_text_alignment(s_notice_layer, GTextAlignmentCenter);
  layer_add_child( window_layer, text_layer_get_layer(s_notice_layer));
  
  if(!fromWakeUp){
    layer_set_hidden(text_layer_get_layer(s_notice_layer), true);
  }
  
  debugOut("start");
}
static void main_window_unload ( Window *window ) {  
    // Destroy TextLayer
    text_layer_destroy(s_notice_layer);
    text_layer_destroy(s_time_layer);
    text_layer_destroy(s_date_layer);
  
  if(fromWakeUp){
  } else {
    // Destroy GBitmap
    gbitmap_destroy(mainBgImg);
    
    // Destroy BitmapLayer
    bitmap_layer_destroy(mainBgImgLyr);
    
    // Destroy TextLayer
    text_layer_destroy(s_total_count_layer);
    text_layer_destroy(s_today_count_layer);
    
    tick_timer_service_unsubscribe();
    
    // Destroy canvas layer
    layer_destroy(s_canvas_layer);
  }
  
}

static void wakeup_handler(WakeupId id, int32_t reason) {
//   //Delete persistent storage value
//   persist_delete(PERSIST_WAKEUP);
  
  text_layer_set_text(s_notice_layer, "\n\n\n\n\nPiA: Sending Pulse!");
  sendMessage(JS_SEND_PULSE);
  layer_set_hidden(text_layer_get_layer(s_notice_layer), false);
  wakeUpInit();
}


int main(void) {
  //persist_write_string(PERSIST_KEY_LOGS, ""); //////////////////////////////////////////////testing delete before publishing
  
  if(launch_reason() == APP_LAUNCH_WAKEUP) {
    fromWakeUp = true;
    appendLog("Wake up");
  }  
  
  // Register callbacks
  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);
  
  
//   if(fromWakeUp) {
//     APP_LOG(APP_LOG_LEVEL_DEBUG, "..................WAKE UUUUUUUUUUUUUUUUUUUUUUUUUUUP");
//     s_main_window = window_create();
//     window_stack_push( window_create(), false);
//   } else {
//     APP_LOG(APP_LOG_LEVEL_DEBUG, "..................NORMAL LAUNCH");
      
    s_main_window = window_create();
    window_set_background_color(s_main_window, GColorBlack);
    // Use this provider to add button click subscriptions
    window_set_click_config_provider(s_main_window, click_config_provider);
    window_set_window_handlers( s_main_window, (WindowHandlers) {
      .load = main_window_load,
      .unload = main_window_unload
    });
  
    window_stack_push(s_main_window, true);
      
    // subscribe to wakeup service to get wakeup events while app is running
    wakeup_service_subscribe(wakeup_handler);
  
  
//   }
  
  
  // Open AppMessage
  const int inbox_size = 1000;
  const int outbox_size = 32;


  no_response_timer = app_timer_register( MESSAGE_TIMEOUT, no_response_timer_callback, NULL);  
  app_message_open(inbox_size, outbox_size);
  
  
  wakeUpInit();
  
  app_event_loop();
  
  window_destroy(s_main_window);
}


