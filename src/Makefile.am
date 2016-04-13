AM_CPPFLAGS = -I$(top_srcdir)/include $(GLIB_CFLAGS) $(SPICEGLIB_CFLAGS) $(FLEXVDI_SPICE_CLIENT_CFLAGS)
AM_LDFLAGS = -no-undefined

lib_LTLIBRARIES=libspiceglue.la
libspiceglue_la_LIBADD=$(GLIB_LIBS) $(SPICEGLIB_LIBS)
libspiceglue_la_SOURCES=glue-printing.c glue-spice-widget.c glue-service.c glue-spicy.c

if WITH_PRINTING
libspiceglue_la_LIBADD +=	$(FLEXVDI_SPICE_CLIENT_LIBS)
endif
