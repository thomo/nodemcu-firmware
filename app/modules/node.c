// Module for interfacing with system

#include "module.h"
#include "lauxlib.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "legc.h"

#include "lopcodes.h"
#include "lstring.h"
#include "lundump.h"

#include "platform.h"
#include "lrodefs.h"

#include "c_types.h"
#include "c_string.h"
#include "driver/uart.h"
#include "user_interface.h"
#include "flash_api.h"
#include "flash_fs.h"
#include "user_version.h"
#include "rom.h"
#include "task/task.h"

#define CPU80MHZ 80
#define CPU160MHZ 160

// Lua: restart()
static int node_restart( lua_State* L )
{
  system_restart();
  return 0;
}

// Lua: dsleep( us, option )
static int node_deepsleep( lua_State* L )
{
  uint32 us;
  uint8 option;
  //us = luaL_checkinteger( L, 1 );
  // Set deleep option, skip if nil
  if ( lua_isnumber(L, 2) )
  {
    option = lua_tointeger(L, 2);
    if ( option < 0 || option > 4)
      return luaL_error( L, "wrong arg range" );
    else
      system_deep_sleep_set_option( option );
  }
  // Set deleep time, skip if nil
  if ( lua_isnumber(L, 1) )
  {
    us = luaL_checknumber(L, 1);
    // if ( us <= 0 )
    if ( us < 0 )
      return luaL_error( L, "wrong arg range" );
    else
      system_deep_sleep( us );
  }
  return 0;
}

// Lua: dsleep_set_options
// Combined to dsleep( us, option )
// static int node_deepsleep_setoption( lua_State* L )
// {
//   s32 option;
//   option = luaL_checkinteger( L, 1 );
//   if ( option < 0 || option > 4)
//     return luaL_error( L, "wrong arg range" );
//   else
//    deep_sleep_set_option( option );
//   return 0;
// }
// Lua: info()

static int node_info( lua_State* L )
{
  lua_pushinteger(L, NODE_VERSION_MAJOR);
  lua_pushinteger(L, NODE_VERSION_MINOR);
  lua_pushinteger(L, NODE_VERSION_REVISION);
  lua_pushinteger(L, system_get_chip_id());   // chip id
  lua_pushinteger(L, spi_flash_get_id());     // flash id
#if defined(FLASH_SAFE_API)
  lua_pushinteger(L, flash_safe_get_size_byte() / 1024);  // flash size in KB
#else
  lua_pushinteger(L, flash_rom_get_size_byte() / 1024);  // flash size in KB
#endif // defined(FLASH_SAFE_API)
  lua_pushinteger(L, flash_rom_get_mode());
  lua_pushinteger(L, flash_rom_get_speed());
  return 8;
}

// Lua: chipid()
static int node_chipid( lua_State* L )
{
  uint32_t id = system_get_chip_id();
  lua_pushinteger(L, id);
  return 1;
}

// deprecated, moved to adc module
// Lua: readvdd33()
// static int node_readvdd33( lua_State* L )
// {
//   uint32_t vdd33 = readvdd33();
//   lua_pushinteger(L, vdd33);
//   return 1;
// }

// Lua: flashid()
static int node_flashid( lua_State* L )
{
  uint32_t id = spi_flash_get_id();
  lua_pushinteger( L, id );
  return 1;
}

// Lua: flashsize()
static int node_flashsize( lua_State* L )
{
  if (lua_type(L, 1) == LUA_TNUMBER)
  {
    flash_rom_set_size_byte(luaL_checkinteger(L, 1));
  }
#if defined(FLASH_SAFE_API)
  uint32_t sz = flash_safe_get_size_byte();
#else
  uint32_t sz = flash_rom_get_size_byte();
#endif // defined(FLASH_SAFE_API)
  lua_pushinteger( L, sz );
  return 1;
}

// Lua: heap()
static int node_heap( lua_State* L )
{
  uint32_t sz = system_get_free_heap_size();
  lua_pushinteger(L, sz);
  return 1;
}

#ifdef DEVKIT_VERSION_0_9
static int led_high_count = LED_HIGH_COUNT_DEFAULT;
static int led_low_count = LED_LOW_COUNT_DEFAULT;
static int led_count = 0;
static int key_press_count = 0;
static bool key_short_pressed = false;
static bool key_long_pressed = false;
static os_timer_t keyled_timer;
static int long_key_ref = LUA_NOREF;
static int short_key_ref = LUA_NOREF;

static void default_long_press(void *arg) {
  if (led_high_count == 12 && led_low_count == 12) {
    led_low_count = led_high_count = 6;
  } else {
    led_low_count = led_high_count = 12;
  }
  // led_high_count = 1000 / READLINE_INTERVAL;
  // led_low_count = 1000 / READLINE_INTERVAL;
  // NODE_DBG("default_long_press is called. hc: %d, lc: %d\n", led_high_count, led_low_count);
}

static void default_short_press(void *arg) {
  system_restart();
}

static void key_long_press(void *arg) {
  lua_State *L = lua_getstate();
  NODE_DBG("key_long_press is called.\n");
  if (long_key_ref == LUA_NOREF) {
    default_long_press(arg);
    return;
  }
  lua_rawgeti(L, LUA_REGISTRYINDEX, long_key_ref);
  lua_call(L, 0, 0);
}

static void key_short_press(void *arg) {
  lua_State *L = lua_getstate();
  NODE_DBG("key_short_press is called.\n");
  if (short_key_ref == LUA_NOREF) {
    default_short_press(arg);
    return;
  }
  lua_rawgeti(L, LUA_REGISTRYINDEX, short_key_ref);
  lua_call(L, 0, 0);
}

static void update_key_led (void *p)
{
  (void)p;
  uint8_t temp = 1, level = 1;
  led_count++;
  if(led_count>led_low_count+led_high_count){
    led_count = 0;    // reset led_count, the level still high
  } else if(led_count>led_low_count && led_count <=led_high_count+led_low_count){
    level = 1;    // output high level
  } else if(led_count<=led_low_count){
    level = 0;    // output low level
  }
  temp = platform_key_led(level);
  if(temp == 0){      // key is pressed
    key_press_count++;
    if(key_press_count>=KEY_LONG_COUNT){
      // key_long_press(NULL);
      key_long_pressed = true;
      key_short_pressed = false;
      // key_press_count = 0;
    } else if(key_press_count>=KEY_SHORT_COUNT){    // < KEY_LONG_COUNT
      // key_short_press(NULL);
      key_short_pressed = true;
    }
  }else{  // key is released
    key_press_count = 0;
    if(key_long_pressed){
      key_long_press(NULL);
      key_long_pressed = false;
    }
    if(key_short_pressed){
      key_short_press(NULL);
      key_short_pressed = false;
    }
  }
}

static void prime_keyled_timer (void)
{
  os_timer_disarm (&keyled_timer);
  os_timer_setfn (&keyled_timer, update_key_led, 0);
  os_timer_arm (&keyled_timer, KEYLED_INTERVAL, 1);
}

// Lua: led(low, high)
static int node_led( lua_State* L )
{
  int low, high;
  if ( lua_isnumber(L, 1) )
  {
    low = lua_tointeger(L, 1);
    if ( low < 0 ) {
      return luaL_error( L, "wrong arg type" );
    }
  } else {
    low = LED_LOW_COUNT_DEFAULT; // default to LED_LOW_COUNT_DEFAULT
  }
  if ( lua_isnumber(L, 2) )
  {
    high = lua_tointeger(L, 2);
    if ( high < 0 ) {
      return luaL_error( L, "wrong arg type" );
    }
  } else {
    high = LED_HIGH_COUNT_DEFAULT; // default to LED_HIGH_COUNT_DEFAULT
  }
  led_high_count = (uint32_t)high / READLINE_INTERVAL;
  led_low_count = (uint32_t)low / READLINE_INTERVAL;
  prime_keyled_timer();
  return 0;
}

// Lua: key(type, function)
static int node_key( lua_State* L )
{
  int *ref = NULL;
  size_t sl;

  const char *str = luaL_checklstring( L, 1, &sl );
  if (str == NULL)
    return luaL_error( L, "wrong arg type" );

  if (sl == 5 && c_strcmp(str, "short") == 0) {
    ref = &short_key_ref;
  } else if (sl == 4 && c_strcmp(str, "long") == 0) {
    ref = &long_key_ref;
  } else {
    ref = &short_key_ref;
  }
  // luaL_checkanyfunction(L, 2);
  if (lua_type(L, 2) == LUA_TFUNCTION || lua_type(L, 2) == LUA_TLIGHTFUNCTION) {
    lua_pushvalue(L, 2);  // copy argument (func) to the top of stack
    if (*ref != LUA_NOREF)
      luaL_unref(L, LUA_REGISTRYINDEX, *ref);
    *ref = luaL_ref(L, LUA_REGISTRYINDEX);
  } else {    // unref the key press function
    if (*ref != LUA_NOREF)
      luaL_unref(L, LUA_REGISTRYINDEX, *ref);
    *ref = LUA_NOREF;
  }

  prime_keyled_timer();
  return 0;
}
#endif

extern lua_Load gLoad;
extern bool user_process_input(bool force);
// Lua: input("string")
static int node_input( lua_State* L )
{
  size_t l = 0;
  const char *s = luaL_checklstring(L, 1, &l);
  if (s != NULL && l > 0 && l < LUA_MAXINPUT - 1)
  {
    lua_Load *load = &gLoad;
    if (load->line_position == 0) {
      c_memcpy(load->line, s, l);
      load->line[l + 1] = '\0';
      load->line_position = c_strlen(load->line) + 1;
      load->done = 1;
      NODE_DBG("Get command:\n");
      NODE_DBG(load->line); // buggy here
      NODE_DBG("\nResult(if any):\n");
      user_process_input(true);
    }
  }
  return 0;
}

static int output_redir_ref = LUA_NOREF;
static int serial_debug = 1;
void output_redirect(const char *str) {
  lua_State *L = lua_getstate();
  // if(c_strlen(str)>=TX_BUFF_SIZE){
  //   NODE_ERR("output too long.\n");
  //   return;
  // }

  if (output_redir_ref == LUA_NOREF || !L) {
    uart0_sendStr(str);
    return;
  }

  if (serial_debug != 0) {
    uart0_sendStr(str);
  }

  lua_rawgeti(L, LUA_REGISTRYINDEX, output_redir_ref);
  lua_pushstring(L, str);
  lua_call(L, 1, 0);   // this call back function should never user output.
}

// Lua: output(function(c), debug)
static int node_output( lua_State* L )
{
  // luaL_checkanyfunction(L, 1);
  if (lua_type(L, 1) == LUA_TFUNCTION || lua_type(L, 1) == LUA_TLIGHTFUNCTION) {
    lua_pushvalue(L, 1);  // copy argument (func) to the top of stack
    if (output_redir_ref != LUA_NOREF)
      luaL_unref(L, LUA_REGISTRYINDEX, output_redir_ref);
    output_redir_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  } else {    // unref the key press function
    if (output_redir_ref != LUA_NOREF)
      luaL_unref(L, LUA_REGISTRYINDEX, output_redir_ref);
    output_redir_ref = LUA_NOREF;
    serial_debug = 1;
    return 0;
  }

  if ( lua_isnumber(L, 2) )
  {
    serial_debug = lua_tointeger(L, 2);
    if (serial_debug != 0)
      serial_debug = 1;
  } else {
    serial_debug = 1; // default to 1
  }

  return 0;
}

static int writer(lua_State* L, const void* p, size_t size, void* u)
{
  UNUSED(L);
  int file_fd = *( (int *)u );
  if ((FS_OPEN_OK - 1) == file_fd)
    return 1;
  NODE_DBG("get fd:%d,size:%d\n", file_fd, size);

  if (size != 0 && (size != fs_write(file_fd, (const char *)p, size)) )
    return 1;
  NODE_DBG("write fd:%d,size:%d\n", file_fd, size);
  return 0;
}

#define toproto(L,i) (clvalue(L->top+(i))->l.p)
// Lua: compile(filename) -- compile lua file into lua bytecode, and save to .lc
static int node_compile( lua_State* L )
{
  Proto* f;
  int file_fd = FS_OPEN_OK - 1;
  size_t len;
  const char *fname = luaL_checklstring( L, 1, &len );
  if ( len > FS_NAME_MAX_LENGTH )
    return luaL_error(L, "filename too long");

  char output[FS_NAME_MAX_LENGTH];
  c_strcpy(output, fname);
  // check here that filename end with ".lua".
  if (len < 4 || (c_strcmp( output + len - 4, ".lua") != 0) )
    return luaL_error(L, "not a .lua file");

  output[c_strlen(output) - 2] = 'c';
  output[c_strlen(output) - 1] = '\0';
  NODE_DBG(output);
  NODE_DBG("\n");
  if (luaL_loadfsfile(L, fname) != 0) {
    return luaL_error(L, lua_tostring(L, -1));
  }

  f = toproto(L, -1);

  int stripping = 1;      /* strip debug information? */

  file_fd = fs_open(output, fs_mode2flag("w+"));
  if (file_fd < FS_OPEN_OK)
  {
    return luaL_error(L, "cannot open/write to file");
  }

  lua_lock(L);
  int result = luaU_dump(L, f, writer, &file_fd, stripping);
  lua_unlock(L);

  if (fs_flush(file_fd) < 0) {   // result codes aren't propagated by flash_fs.h
    // overwrite Lua error, like writer() does in case of a file io error
    result = 1;
  }
  fs_close(file_fd);
  file_fd = FS_OPEN_OK - 1;

  if (result == LUA_ERR_CC_INTOVERFLOW) {
    return luaL_error(L, "value too big or small for target integer type");
  }
  if (result == LUA_ERR_CC_NOTINTEGER) {
    return luaL_error(L, "target lua_Number is integral but fractional value found");
  }
  if (result == 1) {    // result status generated by writer() or fs_flush() fail
    return luaL_error(L, "writing to file failed");
  }

  return 0;
}

// Task callback handler for node.task.post()
static task_handle_t do_node_task_handle;
static void do_node_task (task_param_t task_fn_ref, uint8_t prio)
{
  lua_State* L = lua_getstate();
  lua_rawgeti(L, LUA_REGISTRYINDEX, (int)task_fn_ref);
  luaL_unref(L, LUA_REGISTRYINDEX, (int)task_fn_ref);
  lua_pushinteger(L, prio);
  lua_call(L, 1, 0);
}

// Lua: node.task.post([priority],task_cb) -- schedule a task for execution next
static int node_task_post( lua_State* L )
{
  int n = 1, Ltype = lua_type(L, 1);
  unsigned priority = TASK_PRIORITY_MEDIUM;
  if (Ltype == LUA_TNUMBER) {
    priority = (unsigned) luaL_checkint(L, 1);
    luaL_argcheck(L, priority <= TASK_PRIORITY_HIGH, 1, "invalid  priority");
    Ltype = lua_type(L, ++n);
  }
  luaL_argcheck(L, Ltype == LUA_TFUNCTION || Ltype == LUA_TLIGHTFUNCTION, n, "invalid function");
  lua_pushvalue(L, n);

  int task_fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);

  if (!do_node_task_handle)  // bind the task handle to do_node_task on 1st call
    do_node_task_handle = task_get_id(do_node_task);

  if(!task_post(priority, do_node_task_handle, (task_param_t)task_fn_ref)) {
    luaL_unref(L, LUA_REGISTRYINDEX, task_fn_ref);
    luaL_error(L, "Task queue overflow. Task not posted");
  }
  return 0;
}

// Lua: setcpufreq(mhz)
// mhz is either CPU80MHZ od CPU160MHZ
static int node_setcpufreq(lua_State* L)
{
  // http://www.esp8266.com/viewtopic.php?f=21&t=1369
  uint32_t new_freq = luaL_checkinteger(L, 1);
  if (new_freq == CPU160MHZ){
    REG_SET_BIT(0x3ff00014, BIT(0));
    ets_update_cpu_frequency(CPU160MHZ);
  } else {
    REG_CLR_BIT(0x3ff00014,  BIT(0));
    ets_update_cpu_frequency(CPU80MHZ);
  }
  new_freq = ets_get_cpu_frequency();
  lua_pushinteger(L, new_freq);
  return 1;
}

// Lua: code, reason [, exccause, epc1, epc2, epc3, excvaddr, depc ] = bootreason()
static int node_bootreason (lua_State *L)
{
  const struct rst_info *ri = system_get_rst_info ();
  uint32_t arr[8] = {
    rtc_get_reset_reason(),
    ri->reason,
    ri->exccause, ri->epc1, ri->epc2, ri->epc3, ri->excvaddr, ri->depc
  };
  int i, n = ((ri->reason != REASON_EXCEPTION_RST) ? 2 : 8);
  for (i = 0; i < n; ++i)
    lua_pushinteger (L, arr[i]);
  return n;
}

// Lua: restore()
static int node_restore (lua_State *L)
{
  system_restore();
  return 0;
}

#ifdef LUA_OPTIMIZE_DEBUG
/* node.stripdebug([level[, function]]). 
 * level:    1 don't discard debug
 *           2 discard Local and Upvalue debug info
 *           3 discard Local, Upvalue and lineno debug info.
 * function: Function to be stripped as per setfenv except 0 not permitted.
 * If no arguments then the current default setting is returned.
 * If function is omitted, this is the default setting for future compiles
 * The function returns an estimated integer count of the bytes stripped.
 */
static int node_stripdebug (lua_State *L) {
  int level;

  if (L->top == L->base) {
    lua_pushlightuserdata(L, &luaG_stripdebug );
    lua_gettable(L, LUA_REGISTRYINDEX);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      lua_pushinteger(L, LUA_OPTIMIZE_DEBUG);
    }
    return 1;
  }

  level = luaL_checkint(L, 1);
  if ((level <= 0) || (level > 3)) luaL_argerror(L, 1, "must in range 1-3");

  if (L->top == L->base + 1) {
    /* Store the default level in the registry if no function parameter */
    lua_pushlightuserdata(L, &luaG_stripdebug);
    lua_pushinteger(L, level);
    lua_settable(L, LUA_REGISTRYINDEX);
    lua_settop(L,0);
    return 0;
  }

  if (level == 1) {
    lua_settop(L,0);
    lua_pushinteger(L, 0);
    return 1;
  }

  if (!lua_isfunction(L, 2)) {
    int scope = luaL_checkint(L, 2);
    if (scope > 0) {
      /* if the function parameter is a +ve integer then climb to find function */
      lua_Debug ar;
      lua_pop(L, 1); /* pop level as getinfo will replace it by the function */
      if (lua_getstack(L, scope, &ar)) {
        lua_getinfo(L, "f", &ar);
      }
    }
  }

  if(!lua_isfunction(L, 2) || lua_iscfunction(L, -1)) luaL_argerror(L, 2, "must be a Lua Function");
  // lua_lock(L);
  Proto *f = clvalue(L->base + 1)->l.p;
  // lua_unlock(L);
  lua_settop(L,0);
  lua_pushinteger(L, luaG_stripdebug(L, f, level, 1));
  return 1;
}
#endif

// Lua: node.egc.setmode( mode, [param])
// where the mode is one of the node.egc constants  NOT_ACTIVE , ON_ALLOC_FAILURE,
// ON_MEM_LIMIT, ALWAYS.  In the case of ON_MEM_LIMIT an integer parameter is reqired
// See legc.h and lecg.c.
static int node_egc_setmode(lua_State* L) {
  unsigned mode  = luaL_checkinteger(L, 1);
  unsigned limit = luaL_optinteger (L, 2, 0);

  luaL_argcheck(L, mode <= (EGC_ON_ALLOC_FAILURE | EGC_ON_MEM_LIMIT | EGC_ALWAYS), 1, "invalid mode");
  luaL_argcheck(L, !(mode & EGC_ON_MEM_LIMIT) || limit>0, 1, "limit must be non-zero");

  legc_set_mode( L, mode, limit );
  return 0;
}
//
// Lua: osprint(true/false)
// Allows you to turn on the native Espressif SDK printing
static int node_osprint( lua_State* L )
{
  if (lua_toboolean(L, 1)) {
    system_set_os_print(1);
  } else {
    system_set_os_print(0);
  }

  return 0;  
}

// Module function map

static const LUA_REG_TYPE node_egc_map[] = {
  { LSTRKEY( "setmode" ),           LFUNCVAL( node_egc_setmode ) },
  { LSTRKEY( "NOT_ACTIVE" ),        LNUMVAL( EGC_NOT_ACTIVE ) },
  { LSTRKEY( "ON_ALLOC_FAILURE" ),  LNUMVAL( EGC_ON_ALLOC_FAILURE ) },
  { LSTRKEY( "ON_MEM_LIMIT" ),      LNUMVAL( EGC_ON_MEM_LIMIT ) },
  { LSTRKEY( "ALWAYS" ),            LNUMVAL( EGC_ALWAYS ) },
  { LNILKEY, LNILVAL }
};
static const LUA_REG_TYPE node_task_map[] = {
  { LSTRKEY( "post" ),            LFUNCVAL( node_task_post ) },
  { LSTRKEY( "LOW_PRIORITY" ),    LNUMVAL( TASK_PRIORITY_LOW ) },
  { LSTRKEY( "MEDIUM_PRIORITY" ), LNUMVAL( TASK_PRIORITY_MEDIUM ) },
  { LSTRKEY( "HIGH_PRIORITY" ),   LNUMVAL( TASK_PRIORITY_HIGH ) },
  { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE node_map[] =
{
  { LSTRKEY( "restart" ), LFUNCVAL( node_restart ) },
  { LSTRKEY( "dsleep" ), LFUNCVAL( node_deepsleep ) },
  { LSTRKEY( "info" ), LFUNCVAL( node_info ) },
  { LSTRKEY( "chipid" ), LFUNCVAL( node_chipid ) },
  { LSTRKEY( "flashid" ), LFUNCVAL( node_flashid ) },
  { LSTRKEY( "flashsize" ), LFUNCVAL( node_flashsize) },
  { LSTRKEY( "heap" ), LFUNCVAL( node_heap ) },
#ifdef DEVKIT_VERSION_0_9
  { LSTRKEY( "key" ), LFUNCVAL( node_key ) },
  { LSTRKEY( "led" ), LFUNCVAL( node_led ) },
#endif
  { LSTRKEY( "input" ), LFUNCVAL( node_input ) },
  { LSTRKEY( "output" ), LFUNCVAL( node_output ) },
// Moved to adc module, use adc.readvdd33()
// { LSTRKEY( "readvdd33" ), LFUNCVAL( node_readvdd33) },
  { LSTRKEY( "compile" ), LFUNCVAL( node_compile) },
  { LSTRKEY( "CPU80MHZ" ), LNUMVAL( CPU80MHZ ) },
  { LSTRKEY( "CPU160MHZ" ), LNUMVAL( CPU160MHZ ) },
  { LSTRKEY( "setcpufreq" ), LFUNCVAL( node_setcpufreq) },
  { LSTRKEY( "bootreason" ), LFUNCVAL( node_bootreason) },
  { LSTRKEY( "restore" ), LFUNCVAL( node_restore) },
#ifdef LUA_OPTIMIZE_DEBUG
  { LSTRKEY( "stripdebug" ), LFUNCVAL( node_stripdebug ) },
#endif
  { LSTRKEY( "egc" ),  LROVAL( node_egc_map ) },
  { LSTRKEY( "task" ), LROVAL( node_task_map ) },
#ifdef DEVELOPMENT_TOOLS
  { LSTRKEY( "osprint" ), LFUNCVAL( node_osprint ) },
#endif

// Combined to dsleep(us, option)
// { LSTRKEY( "dsleepsetoption" ), LFUNCVAL( node_deepsleep_setoption) },
  { LNILKEY, LNILVAL }
};

NODEMCU_MODULE(NODE, "node", node_map, NULL);
