/*
 * classes/soup/cookiejar.c - LuakitSoupCookieJar
 *
 * Copyright (C) 2011 Mason Larobina <mason.larobina@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <libsoup/soup-cookie.h>
#include <libsoup/soup-date.h>
#include <libsoup/soup-message.h>
#include <libsoup/soup-session-feature.h>

#include "classes/soup/soup.h"
#include "luah.h"

static void luakit_cookie_jar_session_feature_init(SoupSessionFeatureInterface *interface, gpointer data);
G_DEFINE_TYPE_WITH_CODE (LuakitCookieJar, luakit_cookie_jar, SOUP_TYPE_COOKIE_JAR,
        G_IMPLEMENT_INTERFACE (SOUP_TYPE_SESSION_FEATURE, luakit_cookie_jar_session_feature_init))

inline LuakitCookieJar*
luakit_cookie_jar_new(void)
{
    return g_object_new(LUAKIT_TYPE_COOKIE_JAR, NULL);
}

/* Push all the uri details required from the message SoupURI for the cookie
 * callback to determine the correct cookies to return */
static gint
luaH_push_message_uri(lua_State *L, SoupURI *uri)
{
    lua_createtable(L, 0, 3);
    /* push scheme */
    lua_pushliteral(L, "scheme");
    lua_pushstring(L, uri->scheme);
    lua_rawset(L, -3);
    /* push host */
    lua_pushliteral(L, "host");
    lua_pushstring(L, uri->host);
    lua_rawset(L, -3);
    /* push path */
    lua_pushliteral(L, "path");
    lua_pushstring(L, uri->path);
    lua_rawset(L, -3);
    return 1;
}

static GSList*
cookies_from_table(lua_State *L)
{
    GSList *cookies = NULL;

    /* iterate over cookies table */
    lua_pushnil(L);
    while(luaH_next(L, -2)) {
        /* get cookie */
        cookie_t *c = luaH_toudata(L, -1, &cookie_class);

        /* check type, print error if item invalid */
        if (!c) {
            luaL_error(L, "invalid item in cookies table, got %s",
                lua_typename(L, lua_type(L, -1)));

        /* add cookie's SoupCookie to cookies list */
        } else if (c->cookie)
            /* TODO: check cookie fields */
            cookies = g_slist_prepend(cookies, c->cookie);

        /* remove item */
        lua_pop(L, 1);
    }
    return cookies;
}

gint
luaH_cookiejar_add_cookies(lua_State *L)
{
    SoupCookieJar *soupjar = SOUP_COOKIE_JAR(soupconf.cookiejar);
    LuakitCookieJar *jar = LUAKIT_COOKIE_JAR(soupconf.cookiejar);
    GSList *cookies;

    luaH_checktable(L, 1);
    lua_settop(L, 1);

    if ((cookies = cookies_from_table(L))) {
        jar->silent = TRUE;
        /* add updated cookies to the jar */
        for (GSList *p = cookies; p; p = g_slist_next(p))
            soup_cookie_jar_add_cookie(soupjar, soup_cookie_copy(p->data));
        g_slist_free(cookies);
        jar->silent = FALSE;
    }
    return 0;
}

static void
request_started(SoupSessionFeature *feature, SoupSession *session, SoupMessage *msg, SoupSocket *socket)
{
    (void) session;
    (void) socket;
    SoupCookieJar *soupjar = SOUP_COOKIE_JAR(feature);
    SoupURI* uri = soup_message_get_uri(msg);
    lua_State *L = globalconf.L;

    /* give user a chance to add cookies from other instances into the jar */
    luaH_push_message_uri(L, uri);
    signal_object_emit(L, soupconf.signals, "request-started", 1, 0);

    /* generate cookie header */
    gchar *header = soup_cookie_jar_get_cookies(soupjar, uri, TRUE);
    if (header) {
        soup_message_headers_replace(msg->request_headers, "Cookie", header);
        g_free(header);
    } else
        soup_message_headers_remove(msg->request_headers, "Cookie");
}

/* soup_cookie_equal wasn't good enough */
inline static gboolean
soup_cookie_truly_equal(SoupCookie *c1, SoupCookie *c2)
{
    return (!g_strcmp0(c1->name, c2->name) &&
        !g_strcmp0(c1->value, c2->value)   &&
        !g_strcmp0(c1->path,  c2->path)    &&
        (c1->secure    == c2->secure)      &&
        (c1->http_only == c2->http_only)   &&
        (c1->expires && c2->expires        &&
        (soup_date_to_time_t(c1->expires) == soup_date_to_time_t(c2->expires))));
}

static void
changed(SoupCookieJar *jar, SoupCookie *old, SoupCookie *new)
{
    (void) jar;
    lua_State *L = globalconf.L;

    /* do nothing if cookies are equal */
    if (old && new && soup_cookie_truly_equal(old, new))
        return;

    if (old)
        luaH_cookie_push(L, old);
    else
        lua_pushnil(L);

    if (new)
        luaH_cookie_push(L, new);
    else
        lua_pushnil(L);

    signal_object_emit(L, soupconf.signals, "cookie-changed", 2, 0);
}

static void
finalize(GObject *object)
{
    G_OBJECT_CLASS(luakit_cookie_jar_parent_class)->finalize(object);
}


static void
luakit_cookie_jar_init(LuakitCookieJar *jar)
{
    (void) jar;
}

static void
luakit_cookie_jar_class_init(LuakitCookieJarClass *class)
{
    G_OBJECT_CLASS(class)->finalize       = finalize;
    SOUP_COOKIE_JAR_CLASS(class)->changed = changed;
}

static void
luakit_cookie_jar_session_feature_init(SoupSessionFeatureInterface *interface, gpointer data)
{
    (void) data;
    interface->request_started = request_started;
}

// vim: ft=c:et:sw=4:ts=8:sts=4:tw=80
