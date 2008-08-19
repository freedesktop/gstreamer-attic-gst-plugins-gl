/*
 * GStreamer
 * Copyright (C) 2008 Filippo Argiolas <filippo.argiolas@gmail.com>
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

#include <gstglfilter.h>
#include <gstgleffectssources.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#define GST_TYPE_GL_DIFFERENCEMATTE            (gst_gl_differencematte_get_type())
#define GST_GL_DIFFERENCEMATTE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GL_DIFFERENCEMATTE,GstGLDifferenceMatte))
#define GST_IS_GL_DIFFERENCEMATTE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GL_DIFFERENCEMATTE))
#define GST_GL_DIFFERENCEMATTE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) , GST_TYPE_GL_DIFFERENCEMATTE,GstGLDifferenceMatteClass))
#define GST_IS_GL_DIFFERENCEMATTE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) , GST_TYPE_GL_DIFFERENCEMATTE))
#define GST_GL_DIFFERENCEMATTE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) , GST_TYPE_GL_DIFFERENCEMATTE,GstGLDifferenceMatteClass))

struct _GstGLDifferenceMatte
{
  GstGLFilter filter;

  GstGLShader *shader[4];
  
  gchar *location;
  gboolean bg_has_changed;

  GdkPixbuf *pixbuf;
  GLuint savedbgtexture;
  GLuint newbgtexture;
  GLuint midtexture[4];
  GLuint intexture;
};

struct _GstGLDifferenceMatteClass
{
  GstGLFilterClass filter_class;
};

typedef struct _GstGLDifferenceMatte GstGLDifferenceMatte;
typedef struct _GstGLDifferenceMatteClass GstGLDifferenceMatteClass;

#define GST_CAT_DEFAULT gst_gl_differencematte_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DEBUG_INIT(bla)							\
  GST_DEBUG_CATEGORY_INIT (gst_gl_differencematte_debug, "gldifferencematte", 0, "gldifferencematte element");

GST_BOILERPLATE_FULL (GstGLDifferenceMatte, gst_gl_differencematte, GstGLFilter,
		      GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_differencematte_set_property (GObject * object, guint prop_id,
					 const GValue * value, GParamSpec * pspec);
static void gst_gl_differencematte_get_property (GObject * object, guint prop_id,
					 GValue * value, GParamSpec * pspec);

static void gst_gl_differencematte_init_resources (GstGLFilter* filter);
static void gst_gl_differencematte_reset_resources (GstGLFilter* filter);

static gboolean gst_gl_differencematte_filter (GstGLFilter * filter,
				       GstGLBuffer * inbuf, GstGLBuffer * outbuf);

static const GstElementDetails element_details = GST_ELEMENT_DETAILS (
  "Gstreamer OpenGL DifferenceMatte",
  "Filter/Effect",
  "Saves a background frame and replace it with a pixbuf",
  "Filippo Argiolas <filippo.argiolas@gmail.com>");

enum
{
  PROP_0,
  PROP_LOCATION,
};


/* init resources that need a gl context */
static void
gst_gl_differencematte_init_gl_resources (GstGLFilter *filter)
{
  GstGLDifferenceMatte *differencematte = GST_GL_DIFFERENCEMATTE (filter);
  gint i;

  for (i=0; i<4; i++) {
    glGenTextures (1, &differencematte->midtexture[i]);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, differencematte->midtexture[i]);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
		 filter->width, filter->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); 
    differencematte->shader[i] = gst_gl_shader_new ();
  }

  g_return_if_fail (
    gst_gl_shader_compile_and_check (differencematte->shader[0],
                                     difference_fragment_source,
				     GST_GL_SHADER_FRAGMENT_SOURCE));
  g_return_if_fail (
    gst_gl_shader_compile_and_check (differencematte->shader[1],
                                     hconv9_fragment_source,
				     GST_GL_SHADER_FRAGMENT_SOURCE));
  
  g_return_if_fail (
    gst_gl_shader_compile_and_check (differencematte->shader[2],
                                     vconv9_fragment_source,
				     GST_GL_SHADER_FRAGMENT_SOURCE));
  
  g_return_if_fail (
    gst_gl_shader_compile_and_check (differencematte->shader[3],
                                     texture_interp_fragment_source,
				     GST_GL_SHADER_FRAGMENT_SOURCE));
}

/* free resources that need a gl context */
static void
gst_gl_differencematte_reset_gl_resources (GstGLFilter *filter)
{
  GstGLDifferenceMatte *differencematte = GST_GL_DIFFERENCEMATTE (filter);
  gint i;
  
  glDeleteTextures (1, &differencematte->savedbgtexture);
  glDeleteTextures (1, &differencematte->newbgtexture);
  for (i=0; i<4; i++) {
    g_object_unref (differencematte->shader[i]);
    glDeleteTextures (1, &differencematte->midtexture[i]);
  }
}

static void
gst_gl_differencematte_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &element_details);
}

static void
gst_gl_differencematte_class_init (GstGLDifferenceMatteClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_gl_differencematte_set_property;
  gobject_class->get_property = gst_gl_differencematte_get_property;

  GST_GL_FILTER_CLASS (klass)->filter = gst_gl_differencematte_filter;
  GST_GL_FILTER_CLASS (klass)->display_init_cb = gst_gl_differencematte_init_gl_resources;
  GST_GL_FILTER_CLASS (klass)->display_reset_cb = gst_gl_differencematte_reset_gl_resources;
  GST_GL_FILTER_CLASS (klass)->onStart = gst_gl_differencematte_init_resources;
  GST_GL_FILTER_CLASS (klass)->onStop = gst_gl_differencematte_reset_resources;

  g_object_class_install_property (gobject_class,
                                   PROP_LOCATION,
                                   g_param_spec_string ("location",
                                                        "Background image location", 
                                                        "Background image location",
                                                        NULL, G_PARAM_READWRITE));
}

void
gst_gl_differencematte_draw_texture (GstGLDifferenceMatte * differencematte, GLuint tex)
{
  GstGLFilter *filter = GST_GL_FILTER (differencematte);

  glActiveTexture (GL_TEXTURE0);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, tex);

  glBegin (GL_QUADS);

  glTexCoord2f (0.0, 0.0);
  glVertex2f (-1.0, -1.0);
  glTexCoord2f (filter->width, 0.0);
  glVertex2f (1.0, -1.0);
  glTexCoord2f (filter->width, filter->height);
  glVertex2f (1.0, 1.0);
  glTexCoord2f (0.0, filter->height);
  glVertex2f (-1.0, 1.0);

  glEnd ();
}

static void
gst_gl_differencematte_init (GstGLDifferenceMatte * differencematte, 
                           GstGLDifferenceMatteClass * klass)
{
  differencematte->shader[0] = NULL;
  differencematte->shader[1] = NULL;
  differencematte->shader[2] = NULL;
  differencematte->shader[3] = NULL;
  differencematte->location = NULL;
  differencematte->pixbuf = NULL;
  differencematte->savedbgtexture = 0;
  differencematte->newbgtexture = 0;
  differencematte->bg_has_changed = FALSE;
}

static void
gst_gl_differencematte_reset_resources (GstGLFilter* filter)
{
//  GstGLDifferenceMatte* differencematte = GST_GL_DIFFERENCEMATTE(filter);
}

static void
gst_gl_differencematte_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLDifferenceMatte *differencematte = GST_GL_DIFFERENCEMATTE (object); 

  switch (prop_id) {
  case PROP_LOCATION:
    if (differencematte->location != NULL) g_free (differencematte->location);
    differencematte->bg_has_changed = TRUE;
    differencematte->location = g_value_dup_string (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
gst_gl_differencematte_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLDifferenceMatte *differencematte = GST_GL_DIFFERENCEMATTE (object);

  switch (prop_id) {
  case PROP_LOCATION:
    g_value_set_string (value, differencematte->location);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
gst_gl_differencematte_init_resources (GstGLFilter* filter)
{
//  GstGLDifferenceMatte *differencematte = GST_GL_DIFFERENCEMATTE (filter);
}

static void
gst_gl_differencematte_save_texture (gint width, gint height, guint texture, gpointer stuff)
{
  GstGLDifferenceMatte* differencematte = GST_GL_DIFFERENCEMATTE (stuff);
  
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  gst_gl_differencematte_draw_texture (differencematte, texture);
}

static void init_pixbuf_texture (GstGLDisplay *display, gpointer data)
{
  GstGLDifferenceMatte *differencematte = GST_GL_DIFFERENCEMATTE (data);
  GstGLFilter *filter = GST_GL_FILTER (data);
  
  glDeleteTextures (1, &differencematte->newbgtexture);
  glGenTextures (1, &differencematte->newbgtexture);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, differencematte->newbgtexture);
  glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
                filter->width, filter->height, 0,
                gdk_pixbuf_get_has_alpha (differencematte->pixbuf) ? GL_RGBA : GL_RGB,
                GL_UNSIGNED_BYTE, gdk_pixbuf_get_pixels (differencematte->pixbuf));

  if (differencematte->savedbgtexture == 0) {
    glGenTextures (1, &differencematte->savedbgtexture);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, differencematte->savedbgtexture);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
                 filter->width, filter->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
}

static void
gst_gl_differencematte_diff (gint width, gint height, guint texture, gpointer stuff)
{
  GstGLDifferenceMatte* differencematte = GST_GL_DIFFERENCEMATTE (stuff);
  
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  gst_gl_shader_use (differencematte->shader[0]);

  glActiveTexture (GL_TEXTURE0);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
  glDisable(GL_TEXTURE_RECTANGLE_ARB);
  
  gst_gl_shader_set_uniform_1i (differencematte->shader[0], "current", 0);

  glActiveTexture (GL_TEXTURE1);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, differencematte->savedbgtexture);
  glDisable (GL_TEXTURE_RECTANGLE_ARB);

  gst_gl_shader_set_uniform_1i (differencematte->shader[0], "saved", 1);

  gst_gl_differencematte_draw_texture (differencematte, texture);
}

static void
gst_gl_differencematte_hblur (gint width, gint height, guint texture, gpointer stuff)
{
  GstGLDifferenceMatte* differencematte = GST_GL_DIFFERENCEMATTE (stuff);
  gfloat gauss_kernel[9] = { 
    0.026995, 0.064759, 0.120985,
    0.176033, 0.199471, 0.176033,
    0.120985, 0.064759, 0.026995
  };
  
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  gst_gl_shader_use (differencematte->shader[1]);

  glActiveTexture (GL_TEXTURE0);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
  glDisable(GL_TEXTURE_RECTANGLE_ARB);
  
  gst_gl_shader_set_uniform_1i (differencematte->shader[1], "tex", 0);

  gst_gl_shader_set_uniform_1fv (differencematte->shader[1], "kernel", 9, gauss_kernel);
  gst_gl_shader_set_uniform_1f (differencematte->shader[1], "norm_const", 0.977016f);
  gst_gl_shader_set_uniform_1f (differencematte->shader[1], "norm_offset", 0.0f);

  gst_gl_differencematte_draw_texture (differencematte, texture);
}

static void
gst_gl_differencematte_vblur (gint width, gint height, guint texture, gpointer stuff)
{
  GstGLDifferenceMatte* differencematte = GST_GL_DIFFERENCEMATTE (stuff);
  gfloat gauss_kernel[9] = { 
    0.026995, 0.064759, 0.120985,
    0.176033, 0.199471, 0.176033,
    0.120985, 0.064759, 0.026995
  };
  
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  gst_gl_shader_use (differencematte->shader[2]);

  glActiveTexture (GL_TEXTURE0);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
  glDisable(GL_TEXTURE_RECTANGLE_ARB);

  gst_gl_shader_set_uniform_1i (differencematte->shader[2], "tex", 0);

  gst_gl_shader_set_uniform_1fv (differencematte->shader[2], "kernel", 9, gauss_kernel);
  gst_gl_shader_set_uniform_1f (differencematte->shader[2], "norm_const", 0.977016f);
  gst_gl_shader_set_uniform_1f (differencematte->shader[2], "norm_offset", 0.0f);
  
  gst_gl_differencematte_draw_texture (differencematte, texture);
}

static void
gst_gl_differencematte_interp (gint width, gint height, guint texture, gpointer stuff)
{
  GstGLDifferenceMatte* differencematte = GST_GL_DIFFERENCEMATTE (stuff);
  
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  gst_gl_shader_use (differencematte->shader[3]);

  glActiveTexture (GL_TEXTURE0);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
  glDisable(GL_TEXTURE_RECTANGLE_ARB);
  
  gst_gl_shader_set_uniform_1i (differencematte->shader[3], "blend", 0);

  glActiveTexture (GL_TEXTURE1);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, differencematte->newbgtexture);
  glDisable (GL_TEXTURE_RECTANGLE_ARB);

  gst_gl_shader_set_uniform_1i (differencematte->shader[3], "base", 1);

  glActiveTexture (GL_TEXTURE2);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, differencematte->midtexture[2]);
  glDisable (GL_TEXTURE_RECTANGLE_ARB);

  gst_gl_shader_set_uniform_1i (differencematte->shader[3], "alpha", 2);

  gst_gl_differencematte_draw_texture (differencematte, texture);
}

static void
gst_gl_differencematte_identity (gint width, gint height, guint texture, gpointer stuff)
{
  GstGLDifferenceMatte* differencematte = GST_GL_DIFFERENCEMATTE (stuff);
  
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  gst_gl_differencematte_draw_texture (differencematte, texture);
}

static gboolean
gst_gl_differencematte_filter (GstGLFilter* filter, GstGLBuffer* inbuf,
				GstGLBuffer* outbuf)
{
  GstGLDifferenceMatte* differencematte = GST_GL_DIFFERENCEMATTE(filter);
  GdkPixbuf *pixbuf;
  GError *error = NULL;

  differencematte->intexture = inbuf->texture;

  if (differencematte->bg_has_changed && (differencematte->location != NULL)) {
    pixbuf = gdk_pixbuf_new_from_file (differencematte->location, &error);
    if (pixbuf) {
      differencematte->pixbuf = gdk_pixbuf_scale_simple (pixbuf,
                                                         filter->width,
                                                         filter->height,
                                                         GDK_INTERP_BILINEAR);
      gdk_pixbuf_unref (pixbuf);
      if (differencematte->pixbuf != NULL) {
        gst_gl_display_thread_add (filter->display, init_pixbuf_texture, differencematte);
        /* save current frame, needed to calculate difference between
         * this frame and next ones */
        gst_gl_filter_render_to_target (filter, inbuf->texture, 
                                        differencematte->savedbgtexture,
                                        gst_gl_differencematte_save_texture,
                                        differencematte);
        gdk_pixbuf_unref (differencematte->pixbuf);
      }
    } else {
      if (error != NULL && error->message != NULL)
        g_warning ("unable to load %s: %s", differencematte->location, error->message);
    }
    differencematte->bg_has_changed = FALSE;
  }

  if (differencematte->savedbgtexture != 0) {
    gst_gl_filter_render_to_target (filter,
                                    inbuf->texture, 
                                    differencematte->midtexture[0],
                                    gst_gl_differencematte_diff, differencematte);
    gst_gl_filter_render_to_target (filter, 
                                    differencematte->midtexture[0],
                                    differencematte->midtexture[1],
                                    gst_gl_differencematte_hblur, differencematte);
    gst_gl_filter_render_to_target (filter, 
                                    differencematte->midtexture[1],
                                    differencematte->midtexture[2],
                                    gst_gl_differencematte_vblur, differencematte);
    gst_gl_filter_render_to_target (filter, 
                                    inbuf->texture,
                                    outbuf->texture,
                                    gst_gl_differencematte_interp, differencematte);
  } else {
    gst_gl_filter_render_to_target (filter, 
                                    inbuf->texture,
                                    outbuf->texture,
                                    gst_gl_differencematte_identity, differencematte);
  }
    
  return TRUE;
}