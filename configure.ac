AC_PREREQ([2.67])
AC_INIT([shpotify], [0.1], [giuseppe@spotify.com])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([1.9])

AC_PROG_CC
AM_PROG_CC_C_O

PKG_CHECK_MODULES([LIBSPOTIFY], [libspotify >= 12], [],
                                [echo libspotify not found
                                exit 1])

AC_CHECK_HEADERS([stdlib.h unistd.h ncurses.h])

AC_CHECK_LIB(asound, snd_pcm_open, [],
                     [echo asound not found
                     exit 1])

AC_CHECK_LIB(ncursesw, initscr, [],
                      [echo ncursesw not found
                       exit 1])
AC_CHECK_LIB(menuw, new_menu, [],
                    [libmenuw not found
                    exit 1])
AC_CHECK_LIB(formw, new_form, [],
                    [libecho new_form not found
                    exit 1
                    ])
AC_CHECK_LIB(jpeg, jpeg_mem_init, [],
                   [libjpeg not found
                   exit 1
                   ])

AC_SUBST([LIBSPOTIFY_CFLAGS])
AC_SUBST([LIBSPOTIFY_LIBS])

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
