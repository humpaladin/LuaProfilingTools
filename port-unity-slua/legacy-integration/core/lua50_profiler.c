﻿/*
** LuaProfiler
** Copyright Kepler Project 2005-2007 (http://www.keplerproject.org/luaprofiler)
** $Id: lua50_profiler.c,v 1.16 2008-05-20 21:16:36 mascarenhas Exp $
*/

/*****************************************************************************
lua50_profiler.c:
   Lua version dependent profiler interface
*****************************************************************************/
/*
	解决跨平台编译时宏控制import/export的问题 lennon.c
	2016-08-11 lennon.c
*/ 
#define LUA_CORE
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clocks.h"
#include "core_profiler.h"
#include "function_meter.h"

#include "lua.h"
#include "lauxlib.h"
#include "luaprofiler.h"

/* Indices for the main profiler stack and for the original exit function */
static int exit_id;
static int profstate_id;
static int is_pause;
//static int profresult_id;

/* Forward declaration */
static int profiler_stop(lua_State *L);
//static int profiler_clear(lua_State *L);

/* called by Lua (via the callhook mechanism) */
static void callhook(lua_State *L, lua_Debug *ar) {
  int currentline;
  lua_Debug previous_ar;
  lprofP_STATE* S;
  int stackIndex = -1;
  lua_pushlightuserdata(L, &profstate_id);
  lua_gettable(L, LUA_REGISTRYINDEX);
  S = (lprofP_STATE*)lua_touserdata(L, -1);

  if (lua_getstack(L, 1, &previous_ar) == 0) {
    currentline = -1;
  } else {
    lua_getinfo(L, "l", &previous_ar);
    currentline = previous_ar.currentline;
  }
      
  lua_getinfo(L, "nS", ar);

  stackIndex = lua_gettop(L);

  printf("stacklevel = %d", S->stack_level);

	if (!ar->event) {
		  /* entering a function */
		  lprofP_callhookIN(S, (char *)ar->name,
			  (char *)ar->source, ar->linedefined,
			  currentline,(char *)ar->what);
	  }
	  else { /* ar->event == "return" */
		  lprofP_callhookOUT(S);
	  }

}


/* Lua function to exit politely the profiler                               */
/* redefines the lua exit() function to not break the log file integrity    */
/* The log file is assumed to be valid if the last entry has a stack level  */
/* of 1 (meaning that the function 'main' has been exited)                  */
static void exit_profiler(lua_State *L) {
  lprofP_STATE* S;
  lua_pushlightuserdata(L, &profstate_id);
  lua_gettable(L, LUA_REGISTRYINDEX);
  S = (lprofP_STATE*)lua_touserdata(L, -1);
  /* leave all functions under execution */
  while (lprofP_callhookOUT(S)) ;
  /* call the original Lua 'exit' function */
  lua_pushlightuserdata(L, &exit_id);
  lua_gettable(L, LUA_REGISTRYINDEX);
  lua_call(L, 0, 0);
}

/* Our new coroutine.create function  */
/* Creates a new profile state for the coroutine */
#if 0
static int coroutine_create(lua_State *L) {
  lprofP_STATE* S;
  lua_State *NL = lua_newthread(L);
  luaL_argcheck(L, lua_isfunction(L, 1) && !lua_iscfunction(L, 1), 1,
		"Lua function expected");
  lua_pushvalue(L, 1);  /* move function to top */
  lua_xmove(L, NL, 1);  /* move function from L to NL */
  /* Inits profiler and sets profiler hook for this coroutine */
  S = lprofM_init();
  lua_pushlightuserdata(L, NL);
  lua_pushlightuserdata(L, S);
  lua_settable(L, LUA_REGISTRYINDEX);
  lua_sethook(NL, (lua_Hook)callhook, LUA_MASKCALL | LUA_MASKRET, 0);
  return 1;	
}
#endif

static int profiler_pause(lua_State *L) {
  lprofP_STATE* S;
  lua_pushlightuserdata(L, &profstate_id);
  lua_gettable(L, LUA_REGISTRYINDEX);
  S = (lprofP_STATE*)lua_touserdata(L, -1);
  lprofM_pause_function(S);
  is_pause = 1;
  return 0;
}

static int profiler_resume(lua_State *L) {
  lprofP_STATE* S;
  lua_pushlightuserdata(L, &profstate_id);
  lua_gettable(L, LUA_REGISTRYINDEX);
  S = (lprofP_STATE*)lua_touserdata(L, -1);
  lprofM_pause_function(S);
  is_pause = 0;
  return 0;
}

static int profiler_init(lua_State *L) {
	lprofP_STATE* S;
	const char* outfile;

	lua_pushlightuserdata(L, &profstate_id);
	lua_gettable(L, LUA_REGISTRYINDEX);
	if(!lua_isnil(L, -1)) {
	profiler_stop(L);
	}
	lua_pop(L, 1);

	outfile = NULL;
	if(lua_gettop(L) >= 1)
	outfile = luaL_checkstring(L, 1);

	/* init with default file name and printing a header line */
	if (!(S=lprofP_init_core_profiler(outfile, 1, 0))) {
	return luaL_error(L,"LuaProfiler error: output file could not be opened!");
	}

	lua_sethook(L, (lua_Hook)callhook, LUA_MASKCALL | LUA_MASKRET, 0);

	lua_pushlightuserdata(L, &profstate_id);
	lua_pushlightuserdata(L, S);
	lua_settable(L, LUA_REGISTRYINDEX);
	
	/* use our own exit function instead */
	lua_getglobal(L, "os");
	lua_pushlightuserdata(L, &exit_id);
	lua_pushstring(L, "exit");
	lua_gettable(L, -3);
	lua_settable(L, LUA_REGISTRYINDEX);
	lua_pushstring(L, "exit");
	lua_pushcfunction(L, (lua_CFunction)exit_profiler);
	lua_settable(L, -3);

	#if 0
	/* use our own coroutine.create function instead */
	lua_getglobal(L, "coroutine");
	lua_pushstring(L, "create");
	lua_pushcfunction(L, (lua_CFunction)coroutine_create);
	lua_settable(L, -3);
	#endif

	/* the following statement is to simulate how the execution stack is */
	/* supposed to be by the time the profiler is activated when loaded  */
	/* as a library.                                                     */

	lprofP_callhookIN(S, "profiler_init", "(C)", -1, -1,"C");
	
	lua_pushboolean(L, 1);
	return 1;
}

static int is_profiler_pause(lua_State *L)
{
	lua_pushboolean(L, is_pause);
	return 1;
}

static int profiler_stop(lua_State *L) {
	lprofP_STATE* S;
	lua_sethook(L, (lua_Hook)callhook, 0, 0);
	lua_pushlightuserdata(L, &profstate_id);
	lua_gettable(L, LUA_REGISTRYINDEX);
	if(!lua_isnil(L, -1)) 
	{
		S = (lprofP_STATE*)lua_touserdata(L, -1);
		/* leave all functions under execution */
		while (lprofP_callhookOUT(S))
			;
		lprofP_close_core_profiler(S);
		lua_pushlightuserdata(L, &profstate_id);
		lua_pushnil(L);
		lua_settable(L, LUA_REGISTRYINDEX);
		lua_pushboolean(L, 1);
	} 
	else 
	{ 
		lua_pushboolean(L, 0); 
	}

  return 1;
}

static const luaL_Reg prof_funcs[] = {
  { "profiler_pause", profiler_pause },
  { "profiler_resume", profiler_resume },
  { "profiler_start", profiler_init },
  { "profiler_stop", profiler_stop },
  { NULL, NULL }
};

/*
int luaopen_profiler(lua_State *L) {
  luaL_openlib(L, "profiler", prof_funcs, 0);
  lua_pushliteral (L, "_COPYRIGHT");
  lua_pushliteral (L, "Copyright (C) 2003-2007 Kepler Project");
  lua_settable (L, -3);
  lua_pushliteral (L, "_DESCRIPTION");
  lua_pushliteral (L, "LuaProfiler is a time profiler designed to help finding bottlenecks in your Lua program.");
  lua_settable (L, -3);
  lua_pushliteral (L, "_NAME");
  lua_pushliteral (L, "LuaProfiler");
  lua_settable (L, -3);
  lua_pushliteral (L, "_VERSION");
  lua_pushliteral (L, "2.0.1");
  lua_settable (L, -3);
  return 1;
}
*/

int profiler_open(lua_State *L)
{
	is_pause = 0;

	lua_register(L, "profiler_start", profiler_init);
	lua_register(L, "profiler_pause", profiler_pause);
	lua_register(L, "profiler_resume", profiler_resume);
	lua_register(L, "profiler_stop", profiler_stop);
	// 增加一个判断是否暂停的函数 2016-08-10 lennon.c
	lua_register(L, "is_profiler_pause", is_profiler_pause);

	return 1;
}


LUA_API void init_profiler(lua_State *L)
{
	profiler_open(L);
	pOutputCallback = NULL;
	pUnityObject = NULL;
	pUnityMethod = NULL;
	lprofT_init();
}

LUA_API void frame_profiler(int id, int unitytime)
{
	lprofT_frame(id, unitytime);
}

LUA_API void register_callback(void* pcallback)
{
	if (pcallback)
	{
		pOutputCallback = (pfnoutputCallback)pcallback;
	}
}

LUA_API int isregister_callback()
{
	if (pOutputCallback)
		return 1;
	else
		return 0;
}

LUA_API void unregister_callback()
{
	pOutputCallback = NULL;
	if (pUnityObject)
		free(pUnityObject);
	if (pUnityMethod)
		free(pUnityMethod);
	pUnityObject = pUnityMethod = NULL;

}