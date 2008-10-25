/* 
 * GStreamer
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#undef UNICODE
#include <windows.h>
#define UNICODE

#include "gstglwindow.h"


#define WM_GSTGLWINDOW (WM_APP+1)

void gst_gl_window_set_pixel_format (GstGLWindow *window);
LRESULT CALLBACK window_proc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

#define GST_GL_WINDOW_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_WINDOW, GstGLWindowPrivate))

enum
{
  PROP_0
};

struct _GstGLWindowPrivate
{
  HWND internal_win_id;
  HWND external_win_id;
  HDC device;
  HGLRC gl_context;
  gboolean has_external_window_id;
  gboolean has_external_gl_context;
  GstGLWindowCB draw_cb;
  gpointer draw_data;
  GstGLWindowCB2 resize_cb;
  gpointer resize_data;
  GstGLWindowCB close_cb;
  gpointer close_data;
};

G_DEFINE_TYPE (GstGLWindow, gst_gl_window, G_TYPE_OBJECT);

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "GstGLWindow"

gboolean _gst_gl_window_debug = FALSE;

/* Must be called in the gl thread */
static void
gst_gl_window_finalize (GObject * object)
{
  GstGLWindow *window = GST_GL_WINDOW (object);
  GstGLWindowPrivate *priv = window->priv;

  G_OBJECT_CLASS (gst_gl_window_parent_class)->finalize (object);
}

static void
gst_gl_window_log_handler (const gchar *domain, GLogLevelFlags flags,
                           const gchar *message, gpointer user_data)
{
  if (_gst_gl_window_debug) {
    g_log_default_handler (domain, flags, message, user_data);
  }
}

static void
gst_gl_window_class_init (GstGLWindowClass * klass)
{
  WNDCLASS wc;
  ATOM atom = 0;
  GObjectClass *obj_class = G_OBJECT_CLASS (klass);
  klass->instance = (guint64) GetModuleHandle (NULL);

  g_type_class_add_private (klass, sizeof (GstGLWindowPrivate));

  obj_class->finalize = gst_gl_window_finalize;

  atom = GetClassInfo ((HINSTANCE)klass->instance, "GSTGL", &wc);

  ZeroMemory (&wc, sizeof(WNDCLASS));

  wc.lpfnWndProc = window_proc;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = 0;
  wc.hInstance = (HINSTANCE) klass->instance;
  wc.hIcon = LoadIcon( NULL, IDI_WINLOGO );
  wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
  wc.hCursor = LoadCursor( NULL, IDC_ARROW );
  wc.hbrBackground = (HBRUSH) GetStockObject (BLACK_BRUSH);
  wc.lpszMenuName = NULL;
  wc.lpszClassName = "GSTGL";

  atom = RegisterClass (&wc);
}

static void
gst_gl_window_init (GstGLWindow *window)
{
  window->priv = GST_GL_WINDOW_GET_PRIVATE (window);

  if (g_getenv ("GST_GL_WINDOW_DEBUG") != NULL)
    _gst_gl_window_debug = TRUE;

  g_log_set_handler ("GstGLWindow", G_LOG_LEVEL_DEBUG,
    gst_gl_window_log_handler, NULL);
}

/* Must be called in the gl thread */
GstGLWindow *
gst_gl_window_new (gint width, gint height)
{
  GstGLWindow *window = g_object_new (GST_GL_TYPE_WINDOW, NULL);
  GstGLWindowPrivate *priv = window->priv;
  GstGLWindowClass* klass = GST_GL_WINDOW_GET_CLASS (window);
  
  static gint x = 0;
  static gint y = 0;
  x += 20;
  y += 20;

  priv->internal_win_id = 0;
  priv->external_win_id = 0;
  priv->device = 0;
  priv->gl_context = 0;
  priv->has_external_window_id = FALSE;
  priv->has_external_gl_context = FALSE;
  priv->draw_cb = NULL;
  priv->draw_data = NULL;
  priv->resize_cb = NULL;
  priv->resize_data = NULL;
  priv->close_cb = NULL;
  priv->close_data = NULL;

  width += 2 * GetSystemMetrics (SM_CXSIZEFRAME);
  height += 2 * GetSystemMetrics (SM_CYSIZEFRAME) + GetSystemMetrics (SM_CYCAPTION);

  priv->internal_win_id = CreateWindowEx (
    0,
    "GSTGL",
    "OpenGL renderer",
    WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW,
    x, y, width, height,
    (HWND) NULL,
    (HMENU) NULL,
    (HINSTANCE) klass->instance,
    window
  );

  g_assert (priv->internal_win_id);

  g_debug ("gl window created: %d\n", priv->internal_win_id);

  //device is set in the window_proc 
  g_assert (priv->device);

  UpdateWindow (priv->internal_win_id);
  ShowCursor (TRUE);

  if (wglMakeCurrent (priv->device, priv->gl_context))
    return window;
  else
  {
    g_debug ("Failed to make opengl context current");
    return NULL;
  }
}

GQuark
gst_gl_window_error_quark (void)
{
  return g_quark_from_static_string ("gst-gl-window-error");
}

void 
gst_gl_window_set_external_window_id (GstGLWindow *window, guint64 id)
{
  g_warning ("gst_gl_window_set_external_window_id: not implemented\n");
}

void 
gst_gl_window_set_external_gl_context (GstGLWindow *window, guint64 context)
{
  g_warning ("gst_gl_window_set_external_gl_context: not implemented\n");
}

/* Must be called in the gl thread */
void
gst_gl_window_set_draw_callback (GstGLWindow *window, GstGLWindowCB callback, gpointer data)
{
  GstGLWindowPrivate *priv = window->priv;
  
  priv->draw_cb = callback;
  priv->draw_data = data;
}

/* Must be called in the gl thread */
void
gst_gl_window_set_resize_callback (GstGLWindow *window, GstGLWindowCB2 callback , gpointer data)
{
  GstGLWindowPrivate *priv = window->priv;

  priv->resize_cb = callback;
  priv->resize_data = data;
}

/* Must be called in the gl thread */
void
gst_gl_window_set_close_callback (GstGLWindow *window, GstGLWindowCB callback, gpointer data)
{
  GstGLWindowPrivate *priv = window->priv;

  priv->close_cb = callback;
  priv->close_data = data;
}

/* Must be called in the gl thread */
gboolean
gst_gl_window_has_external_window_id (GstGLWindow *window)
{
  gboolean has_internal_window_id = TRUE;
  GstGLWindowPrivate *priv = window->priv;
  
  has_internal_window_id = priv->has_external_window_id;

  return has_internal_window_id;
}

/* Must be called in the gl thread */
gboolean
gst_gl_window_has_internal_gl_context (GstGLWindow *window)
{
  gboolean has_external_gl_context = TRUE;
  GstGLWindowPrivate *priv = window->priv;
  
  
  has_external_gl_context = priv->has_external_gl_context;
  

  return has_external_gl_context;
}

/* Must be called in the gl thread */
guint64 gst_gl_window_get_window_id (GstGLWindow *window)
{
  g_warning ("gst_gl_window_get_window_id: not implemented\n");

  return 0;
}

/* Must be called in the gl thread */
guint64
gst_gl_window_get_gl_context (GstGLWindow *window)
{
  g_warning ("gst_gl_window_get_gl_context: not implemented\n");

  return 0;
}

/* Thread safe */
void
gst_gl_window_visible (GstGLWindow *window, gboolean visible)
{
  GstGLWindowPrivate *priv = window->priv;
  BOOL ret = FALSE; 
  
  if (visible)
    ret = ShowWindow (priv->internal_win_id, SW_SHOW);
  else
    ret = ShowWindow (priv->internal_win_id, SW_HIDE);
}

/* Thread safe */
void
gst_gl_window_draw (GstGLWindow *window)
{
  GstGLWindowPrivate *priv = window->priv;
  
  if (!priv->has_external_window_id)
    RedrawWindow (priv->internal_win_id, NULL, NULL,
      RDW_NOERASE | RDW_INTERNALPAINT | RDW_INVALIDATE /*| RDW_UPDATENOW*/);
  else
  {
    PAINTSTRUCT ps;
    
    /*RECT destsurf_rect;
    POINT dest_surf_point;

    dest_surf_point.x = 0;
    dest_surf_point.y = 0;
    ClientToScreen (priv->external_win_id, &dest_surf_point);
    GetClientRect (priv->external_win_id, &destsurf_rect);
    OffsetRect (&destsurf_rect, dest_surf_point.x, dest_surf_point.y);

    if (window->State.Width != (destsurf_rect.right - destsurf_rect.left) ||
        window->State.Height != (destsurf_rect.bottom - destsurf_rect.top))
    {
        window->State.Width = destsurf_rect.right - destsurf_rect.left;
        window->State.Height = destsurf_rect.bottom - destsurf_rect.top;
        window->State.NeedToResize = GL_FALSE;
        if( FETCH_WCB( *window, Reshape ) )
            INVOKE_WCB( *window, Reshape, ( window->State.Width, window->State.Height ) );
        glViewport( 0, 0, window->State.Width, window->State.Height );
    }*/

    BeginPaint (priv->external_win_id, &ps);
    priv->draw_cb (priv->draw_data);  //FIXME: wrong thread caller
    glFlush();
    SwapBuffers (priv->device);
    EndPaint (priv->external_win_id, &ps);
  }
}

/* Thread safe */
void
gst_gl_window_resize (GstGLWindow *window, gint width, gint height)
{
  GstGLWindowPrivate *priv = window->priv;
  gint x = 0;
  gint y = 0;
  RECT winRect;

  GetWindowRect (priv->internal_win_id, &winRect);
  x = winRect.left;
  y = winRect.top;

  width += 2 * GetSystemMetrics (SM_CXSIZEFRAME);
  height += 2 * GetSystemMetrics (SM_CYSIZEFRAME) + GetSystemMetrics (SM_CYCAPTION);

  SetWindowPos (priv->internal_win_id, HWND_TOP, x, y, width, height,
    SWP_ASYNCWINDOWPOS | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING | SWP_NOZORDER);
}

void
gst_gl_window_run_loop (GstGLWindow *window)
{
  GstGLWindowPrivate *priv = window->priv;
  gboolean running = TRUE;
  gboolean bRet = FALSE;
  MSG msg;

  g_debug ("begin loop\n");

  while (running && (bRet = GetMessage (&msg, NULL, 0, 0)) != 0)
  { 
      if (bRet == -1)
      {
          g_debug ("error in gst_gl_window_run_loop\n");
          running = FALSE;
      }
      else
      {
          TranslateMessage (&msg); 
          DispatchMessage (&msg); 
      }
  }

  g_debug ("end loop\n");
}

/* Thread safe */
void
gst_gl_window_quit_loop (GstGLWindow *window)
{
  if (window)
  {
    GstGLWindowPrivate *priv = window->priv;
    LRESULT res = PostMessage(priv->internal_win_id, WM_CLOSE, 0, 0);
    g_assert (SUCCEEDED (res));
    g_debug ("end loop requested\n");
  }
}

/* Thread safe */
void
gst_gl_window_send_message (GstGLWindow *window, GstGLWindowCB callback, gpointer data)
{
  if (window)
  {
    GstGLWindowPrivate *priv = window->priv;
    LRESULT res = SendMessage (priv->internal_win_id, WM_GSTGLWINDOW, (WPARAM) data, (LPARAM) callback);
    g_assert (SUCCEEDED (res));
  }
}

/* PRIVATE */

void
gst_gl_window_set_pixel_format (GstGLWindow *window)
{
    GstGLWindowPrivate *priv = window->priv;
    PIXELFORMATDESCRIPTOR pfd;
    gint pixelformat = 0;
    gboolean res = FALSE;

    pfd.nSize           = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion        = 1;
    pfd.dwFlags         = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType      = PFD_TYPE_RGBA;
    pfd.cColorBits      = 24;
    pfd.cRedBits        = 0;
    pfd.cRedShift       = 0;
    pfd.cGreenBits      = 0;
    pfd.cGreenShift     = 0;
    pfd.cBlueBits       = 0;
    pfd.cBlueShift      = 0;
    pfd.cAlphaBits      = 0;
    pfd.cAlphaShift     = 0;
    pfd.cAccumBits      = 0;
    pfd.cAccumRedBits   = 0;
    pfd.cAccumGreenBits = 0;
    pfd.cAccumBlueBits  = 0;
    pfd.cAccumAlphaBits = 0;
    pfd.cDepthBits      = 24;
    pfd.cStencilBits    = 8;
    pfd.cAuxBuffers     = 0;
    pfd.iLayerType      = PFD_MAIN_PLANE;
    pfd.bReserved       = 0;
    pfd.dwLayerMask     = 0;
    pfd.dwVisibleMask   = 0;
    pfd.dwDamageMask    = 0;

    pfd.cColorBits = (BYTE) GetDeviceCaps (priv->device, BITSPIXEL);

    pixelformat = ChoosePixelFormat (priv->device, &pfd );
    
    g_assert (pixelformat);

    res = SetPixelFormat (priv->device, pixelformat, &pfd);

    g_assert (res);
}

LRESULT CALLBACK window_proc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  if (uMsg == WM_CREATE) {

    GstGLWindow *window = (GstGLWindow *) (((LPCREATESTRUCT) lParam)->lpCreateParams);

    g_debug ("WM_CREATE\n");

    SetWindowLongPtr (hWnd, GWLP_USERDATA, (LONG)(guint64)(gpointer) window);

    g_assert (window);

    {
      GstGLWindowPrivate *priv = window->priv;
      priv->device = GetDC (hWnd);
      gst_gl_window_set_pixel_format (window);
      priv->gl_context = wglCreateContext (priv->device);
      if (priv->gl_context)
        g_debug ("gl context created: %d\n", priv->gl_context);
      else
        g_debug ("failed to create glcontext %d\n", hWnd);
      g_assert (priv->gl_context);
      ReleaseDC (hWnd, priv->device);
    }

    return 0;
  }
  else if (GetWindowLongPtr(hWnd, GWLP_USERDATA)) {

    GstGLWindow *window = (GstGLWindow *) (guint64) GetWindowLongPtr(hWnd, GWLP_USERDATA);
    GstGLWindowPrivate *priv = NULL;

    g_assert (window);

    priv = window->priv;

    g_assert (priv);

    g_assert (priv->internal_win_id == hWnd);

    switch ( uMsg ) {

      case WM_SIZE:
      {
        if (priv->resize_cb)
          priv->resize_cb (priv->resize_data, LOWORD(lParam), HIWORD(lParam));
        break;
      }

      case WM_PAINT:
      { 
        if (priv->draw_cb)
        {
          PAINTSTRUCT ps;
          BeginPaint (hWnd, &ps);
          priv->draw_cb (priv->draw_data);
          SwapBuffers (priv->device);
          EndPaint (hWnd, &ps);
        }
        break;
      }

      case WM_CLOSE:
      {
        g_debug ("WM_CLOSE\n");
        if (priv->close_cb)
          priv->close_cb (priv->close_data);
        wglMakeCurrent (NULL, NULL);

        if (priv->gl_context)
          wglDeleteContext (priv->gl_context);

        if (priv->internal_win_id)
          DestroyWindow(priv->internal_win_id);

        SetWindowLongPtr (hWnd, GWLP_USERDATA, 0);
        DestroyWindow(hWnd);
        PostQuitMessage (0);
        break;
      }

      case WM_CAPTURECHANGED:
      {
        g_debug ("WM_CAPTURECHANGED\n");
        if (priv->draw_cb)
          priv->draw_cb (priv->draw_data);
        break;
      }

      case WM_GSTGLWINDOW:
      {
        GstGLWindowCB custom_cb = (GstGLWindowCB) lParam;
        custom_cb ((gpointer) wParam);
        break;
      }

      case WM_ERASEBKGND:
        return TRUE;

      default:
        return DefWindowProc( hWnd, uMsg, wParam, lParam );
    }

    return 0;
  }
  else
    return DefWindowProc( hWnd, uMsg, wParam, lParam );
}