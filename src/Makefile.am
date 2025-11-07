AM_CPPFLAGS = $(LIBCURL_CFLAGS) $(NCURSES_CFLAGS) -I$(top_srcdir)/src
AM_CFLAGS = -std=c11 -Wall -Wextra -Wpedantic

bin_PROGRAMS = deepseek_mpi deepseek_wrapper

deepseek_mpi_SOURCES = \
	main.c \
	app_config.c app_config.h \
	cli.c cli.h \
	tui.c tui.h \
	api_client.c api_client.h \
	input_chunker.c input_chunker.h \
	logger.c logger.h \
	string_buffer.c string_buffer.h \
 	file_loader.c file_loader.h \
	deepseek.h

deepseek_mpi_LDADD = $(LIBCURL_LIBS) $(NCURSES_LIBS)

deepseek_wrapper_SOURCES = \
	deepseek_wrapper.c \
	string_buffer.c string_buffer.h

deepseek_wrapper_LDADD = $(NCURSES_LIBS)

EXTRA_DIST = deepseek.h
