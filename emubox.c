/*
 * BSD 2-Clause License
 *
 * Copyright (c) 2024, rilysh
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <dirent.h>
#include <getopt.h>
#include <locale.h>
#include <ncurses.h>
#include <sys/stat.h>
#include <sys/wait.h>

#ifndef PATH_86BOX
/* Define the path where 86box lives. */
#  define PATH_86BOX "./86Box.AppImage"
// #  error PATH_86BOX variable must be set.
#endif

/* Constants. */
enum {
	OPT_INIT        = 1,
	OPT_NEW         = 2,
	OPT_DELETE      = 3,
	OPT_PURGE       = 4,
	OPT_SELECT      = 5,
	OPT_SETTINGS    = 6,
	OPT_FULLSCREEN  = 7,
	OPT_LANGUAGE    = 8,
	OPT_HELP        = 9,
};

/* Structure for emubox options. */
struct emubox_opts {
	/* Arg: --init */
	int init_opt;
	/* Arg: --new */ 
	int new_opt;
	/* Arg: --delete */
	int delete_opt;
	/* Arg: --purge */
	int purge_opt;
	/* Arg: --select */
	int select_opt;
	/* Arg: --settings */
	int settings_opt;
	/* Arg: --fullscreen */
	int fullscreen_opt;
};

/* Structure for emu_content_len(...) */
struct content_len_info {
	/* Length of the file name. */
	size_t name_sz;

	/* Length of the column from 0 to n. */
	size_t column_sz;

	/* Length of the largest row. */
	size_t row_sz;
};

/* Function prototypes. */
static char *emu_basename(char *path);
static int qsort_compare(const void *s0, const void *s1);
static void emu_exec_shell(const char *args);
static void emu_launch_box(const char *conf, const char *lang,
			   int is_fullscreen);
static char *emu_read_conf(const char *conf);
static char *emu_get_valueof(char *conf_raw, const char *key);
static void emu_init_directory(void);
static char *emu_get_directory(void);
static void emu_content_len(struct content_len_info *clinfo);
static void emu_select_list(const char *lang, int is_fullscreen, int);
static void emu_init_emubox(void);
static void emu_bulk_purge_configs(void);
static void emu_purge_config(const char *name);
static void emu_launch_settings(const char *name);
static void emu_create_new(const char *name);
static void usage(int status);

/* Consume everything before and after, until there's no "/". */
static char *emu_basename(char *path)
{
	char *p;

	p = path;
	while (strchr(p, '/') != NULL)
	        p++;

	return (p);
}

/* qsort's internal function. */
static int qsort_compare(const void *s0, const void *s1)
{
	/* See: https://man7.org/linux/man-pages/man3/qsort.3.html#EXAMPLES
	   especially the commented part. Additionally drop the const qualifier
	   as it will get stripped anyway. */
	return (strcmp(*(char **)s0, *(char **)s1));
}

/* Execute the shell and let shell execute passed arguments. */
static void emu_exec_shell(const char *args)
{
	pid_t pid;
	int ret;

	pid = fork();
	if (pid == (pid_t)-1)
		err(EXIT_FAILURE, "fork");

	if (pid == (pid_t)0) {
		ret = execl("/bin/sh", "sh", "-c", args, (char *)NULL);
		if (ret == -1)
			err(127, "execl");
	}

	while (waitpid(pid, NULL, 0) < 0);
}

/* Does 86box lives in that provided path? */
static void emu_is_86box(void)
{
	struct stat st;

	if (stat(PATH_86BOX, &st) == -1) {
		if (errno == ENOENT)
			fputs("emubox: could not find 86box binary file.\n",
			      stderr);
		else
			warn("stat");

		exit(EXIT_FAILURE);
        }
}

/* Launch the 86box with or without arguments. */
static void emu_launch_box(
	const char *conf, const char *lang,
	int is_fullscreen)
{
	int fd, ret;
	pid_t pid;

	pid = fork();
	if (pid == (pid_t)0) {
		fd = open("/dev/null", O_WRONLY);
		if (fd == -1) {
			/* A rather half-baked situation.
			   Kill the forked process and exit,
			   if this happened. */
		        kill(pid, SIGKILL);
			err(EXIT_FAILURE, "open");
		}

		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (lang) {
			if (is_fullscreen)
				ret = execl(PATH_86BOX, PATH_86BOX,
					    "-C", conf, "-G", lang,
					    "-F", (char *)NULL);
			else
				ret = execl(PATH_86BOX, PATH_86BOX,
					    "-C", conf, "-G", lang,
					    (char *)NULL);
		} else {
			if (is_fullscreen)
				ret = execl(PATH_86BOX, PATH_86BOX,
					    "-C", conf, "-F",
					    (char *)NULL);
			else
				ret = execl(PATH_86BOX, PATH_86BOX,
					    "-C", conf, (char *)NULL);
		}
		if (ret == -1) {
			close(fd);
			/* May yield errors, we'll ignore them. */
			kill(pid, SIGKILL);
		        err(127, "execl");
		}

		close(fd);
		while (waitpid(pid, NULL, 0) == 0);
	}
}

/* Open 86box settings window of a configuration file. */
static void emu_launch_settings(const char *conf)
{
	int fd, ret;
	pid_t pid;

	pid = fork();
	if (pid == (pid_t)0) {
		fd = open("/dev/null", O_WRONLY);
		if (fd == -1) {
			/* A rather half-baked situation.
			   Kill the forked process and exit,
			   if this happened. */
		        kill(pid, SIGKILL);
			err(EXIT_FAILURE, "open");
		}

		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
	        ret = execl(PATH_86BOX, PATH_86BOX,
			    "-C", conf, "-S",
			    (char *)NULL);
		if (ret == -1) {
			close(fd);
			/* May yield errors, we'll ignore them. */
			kill(pid, SIGKILL);
		        err(127, "execl");
		}

		close(fd);
		while (waitpid(pid, NULL, 0) == 0);
	}
}

/* Creates ".emubox" directory, under your profile directory. */
static void emu_init_directory(void)
{
	char *env, *p;
	size_t sz;

	env = getenv("HOME");
	if (env == NULL) {
	        fputs("emubox: "
		      "$HOME environment variable is not set.\n",
		      stderr);
		exit(EXIT_FAILURE);
	}

	/* Length of $HOME and "/.emubox". */ 
	sz = strlen(env) + (size_t)9;
	p = calloc(sz, sizeof(char));
	if (p == NULL)
		err(EXIT_FAILURE, "calloc");

	snprintf(p, sz, "%s/.emubox", env);

	if (mkdir(p, 0700) == -1) {
		if (errno == EEXIST)
		        fputs("emubox: "
			      "emubox config directory already exists.\n",
			      stderr);
		else
		        warn("mkdir");
		free(p);
		exit(EXIT_FAILURE);
	}

	fputs("emubox: done: "
	      "emubox directory has been created.\n",
	      stdout);
	free(p);
}

/* Get the absolute path of ".emubox" directory. */
static char *emu_get_directory(void)
{
	char *env, *p;
	struct stat st;
	size_t sz;

	env = getenv("HOME");
	if (env == NULL) {
	        fputs("emubox: "
		      "$HOME environment variable is not set.\n",
		      stderr);
	        return (NULL);
	}

	/* Length of $HOME and "/.emubox". */ 
	sz = strlen(env) + (size_t)9;
	p = calloc(sz, sizeof(char));
	if (p == NULL)
	        err(EXIT_FAILURE, "calloc");

	snprintf(p, sz, "%s/.emubox/", env);
	if (stat(p, &st) == -1) {
		fputs("emubox: config directory wasn't found.\n",
		      stderr);
		free(p);
		return (NULL);
	}

        return (p);
}

/* Retrieve information about the file name, column,
   and largest rows length. */
static void emu_content_len(struct content_len_info *clinfo)
{
	char *path;
	DIR *dir;
	struct dirent *den;
        size_t tsz;

	path = emu_get_directory();
	if (path == NULL)
		exit(EXIT_FAILURE);
	
        dir = opendir(path);
	if (dir == NULL) {
		free(path);
		err(EXIT_FAILURE, "opendir");
	}

        tsz = clinfo->name_sz = clinfo->column_sz = clinfo->row_sz = (size_t)0;
	while ((den = readdir(dir)) != NULL) {
		if (strcmp(den->d_name, ".") == 0 ||
		    strcmp(den->d_name, "..") == 0)
			continue;

		/* Only normal files are allowed. */
		if (den->d_type == DT_REG) {
			tsz = strlen(den->d_name);
			if (tsz > clinfo->row_sz)
				clinfo->row_sz = tsz;

			clinfo->name_sz += tsz;
		        /* Column size, no bigger than 10 and
			   only increase it's value if it's
			   smaller than that. */
			if (clinfo->column_sz < (size_t)10)
				clinfo->column_sz += (size_t)1;
		}
        }

	clinfo->column_sz += (size_t)5;
	clinfo->row_sz += (size_t)13;
        closedir(dir);
	free(path);
}

/* Creates a (n)curses based menu to select the choice. */
static void emu_select_list(const char *lang, int is_fullscreen, int is_settings)
{
	DIR *dir;
	struct dirent *den;
	char *path, **arr, *base, *p;
	int pre_idx, new_idx, run_idx,
		tmp_idx, ch, end_page,
		xs, xw;
	size_t sz, isz;
	WINDOW *win;
	struct content_len_info clinfo;
	struct stat st;

	tmp_idx = -1;
	pre_idx = new_idx = run_idx = 0;

	path = emu_get_directory();
	if (path == NULL)
		exit(EXIT_FAILURE);

	dir = opendir(path);
	if (dir == NULL) {
	        fputs("emubox: missing config directory.\n",
		      stderr);
		free(path);
	        exit(EXIT_FAILURE);
	}
        emu_content_len(&clinfo);

        arr = calloc((size_t)clinfo.name_sz + 1, sizeof(char *));
	if (arr == NULL) {
	        free(path);
		closedir(dir);
		err(EXIT_FAILURE, "calloc");
	}

	while ((den = readdir(dir)) != NULL) {
		if (strcmp(den->d_name, ".") == 0 ||
		    strcmp(den->d_name, "..") == 0)
			continue;

		/* Only normal files are allowed. */
		if (den->d_type == DT_REG) {			
			sz = strlen(den->d_name);
			arr[pre_idx] = calloc(sz + 1, sizeof(char));
			if (arr[pre_idx] == NULL) {
			        free(path);
				free(arr);
				closedir(dir);
			        err(EXIT_FAILURE, "calloc");
			}

			/* Append to the array. */
			memcpy(arr[pre_idx] + strlen(arr[pre_idx]),
			       den->d_name, sz);
		        pre_idx++;
		}
        }

	/* There are no config files to list. */
	if (pre_idx == 0) {
		fputs("emubox: no configs are available.\n",
		      stderr);
		goto out_cleanup;
        }

	/* Sort the array. */
	qsort(arr, pre_idx, sizeof(char *), qsort_compare);

	/* Initialize ncurses and setup the window. */
	initscr();
        raw();
	noecho();
	keypad(stdscr, TRUE);
	curs_set(FALSE);
        win = newwin(clinfo.column_sz, clinfo.row_sz, 0, 0);
        wrefresh(win);
	keypad(win, TRUE);

	/* Temporary variables we need in the loop. */
        xs = xw = 0;
        end_page = 0;
	tmp_idx = pre_idx;

	for (;;) {
		wclear(win);
		wrefresh(win);
	        mvwprintw(win, 1, 2, "Select a config");
		for (isz = 0; isz < clinfo.row_sz; isz++)
			mvwprintw(win, 2, isz, "\u2500");
	        box(win, 0, 0);
		move(0, 0);

		for (new_idx = xs; new_idx < (xs + 10); new_idx++, xw++) {
			if (new_idx == run_idx)
				wattron(win, A_REVERSE);

			/* Check the current index'd array, whether it has a member
			   or not. */
			if (arr[new_idx] == NULL) {
				end_page = 1;
				break;
			}

			/* HACK: Did we exceed row size of index numbers?
			   If yes, increase it slightly. */

			/* If the index is below or equal to 9 (e.g. 1 to 9). */
			if ((new_idx + 1) <= 9) {
				mvwprintw(win, xw + 3, 4, "%d. ", new_idx + 1);
				mvwprintw(win, xw + 3, 7, "%s", arr[new_idx]);
			}

			/* If the index is higher than 9 (e.g. 9 to n). */
			if ((new_idx + 1) > 9) {
				mvwprintw(win, xw + 3, 3, "%d. ", new_idx + 1);
			        mvwprintw(win, xw + 3, 7, "%s", arr[new_idx]);
			}

			/* If the index is higher than 99 (e.g. 100 to n).
			   It also adds limits for the previous one (e.g. 9 to 99). */
		        if ((new_idx + 1) > 99) {
			        mvwprintw(win, xw + 3, 2, "%d. ", new_idx + 1);
			        mvwprintw(win, xw + 3, 7, "%s", arr[new_idx]);
			}

			/* If the index is higher than 999 (e.g. 999 to n).
			   It adds limits to the previous one (e.g. 100 to 999). */
			if ((new_idx + 1) > 999) {
				mvwprintw(win, xw + 3, 1, "%d. ", new_idx + 1);
			        mvwprintw(win, xw + 3, 7, "%s", arr[new_idx]);
			}

			/* It will be very slow to iterate more than 9999 files.
			   As it's also doing a qsort(), I don't think it's a
			   viable choice to keep a space for other numbersd to
			   fit in here. */
			if ((new_idx + 1) > 9999) {
				endwin();
				fputs("emubox: out of range.\n", stderr);
				goto out_cleanup;
			}

			/* Disable selection color for others. */ 
			if (new_idx == run_idx)
				wattroff(win, A_REVERSE);

			/* Check the next index'd array, whether it has a member
			   or not. */
			if (arr[new_idx + 1] == NULL) {
				end_page = 1;
				break;
			}
		}

		/* Reset the counter */
		xw = 0;

		ch = wgetch(win);
		switch (ch) {
		case KEY_UP:
			/* Decrease the index and check the limit of it. */
			run_idx--;
			if (run_idx < 0)
				run_idx = 0;
			break;

		case KEY_DOWN:
			/* Increase the index and check the limit of it. */
		        run_idx++;
			if (run_idx >= pre_idx)
				run_idx = pre_idx - 1;
			break;

		case KEY_RIGHT:
			/* Go every 10 step forward. */

			/* Do we have reached the end of the page? */
		        if (end_page == 0) {
				wclear(win);
				wrefresh(win);
			        move(0, 0);
				/* Increase page size. (next page) */
				xs += 10;
				/* Set the selection as it's the
				   first member in that list (next). */
			        run_idx = xs;	
			}
			break;

		case KEY_LEFT:
			/* Go every 10 step back. */

			/* Are we bigger than xs? (such as xs != 0) */ 
			if (xs > 0) {
				wclear(win);
				wrefresh(win);
			        move(0, 0);
				/* Decrease page size. (previous page) */
				xs -= 10;
				/* Set the selection as it's the
				   first member in that list (previous). */
				run_idx = xs;
			        end_page = 0;
			}
			break;

		case KEY_BACKSPACE:
			/* Exit from the selection loop and do some
			   after cleanup. */
		        endwin();
			for (; tmp_idx >= 0; tmp_idx--)
				free(arr[tmp_idx]);
		        free(arr);
			free(path);
			closedir(dir);
			_Exit(EXIT_SUCCESS);
			break;

		default:
			break;
		}

		/* Break if this is a a new line or a new line is created.
		   Apperently KEY_ENTER in ncurses is described as send.
		   See: https://invisible-island.net/ncurses/man/curs_getch.3x.html#h2-NOTES */
		if (ch == 10)
			break;
	}
	refresh();
        endwin();

	free(path);
	path = emu_get_directory();
	if (path == NULL) {
		/* Free our allocated resources. */
		for (; tmp_idx >= 0; tmp_idx--)
			free(arr[tmp_idx]);
		free(arr);
	        exit(EXIT_FAILURE);
	}

	sz = strlen(path) + strlen(arr[run_idx]) + (size_t)3;
	p = calloc(sz, sizeof(char));
	if (p == NULL)
		goto out_cleanup;

	snprintf(p, sz, "%s/%s", path, arr[run_idx]);
	base = emu_basename(p);
	if (stat(p, &st) == -1) {
		if (errno == ENOENT)
			fprintf(stderr,
				"emubox: config \"%s\" does not exists.\n",
				base);
		else
			warn("stat");
		free(p);
		goto out_cleanup;	
	}

	fprintf(stdout, "emubox: using config: %s\n", base);
	if (is_settings)
		emu_launch_settings(p);
	else
		emu_launch_box(p, lang, is_fullscreen);
	free(p);

/* Free our older allocated resources. */
out_cleanup:
        for (; tmp_idx >= 0; tmp_idx--)
		free(arr[tmp_idx]);
	free(arr);
	free(path);
	closedir(dir);
}

/* Initialize the emubox directory. */
static void emu_init_emubox(void)
{
	char *path;

	path = emu_get_directory();
	if (path == NULL)
		exit(EXIT_FAILURE);

	if (mkdir(path, 0700) == -1) {
		if (errno == EEXIST)
			fputs("emubox: "
			      "config directory: already exists.\n",
			      stderr);
		else
			warn("mkdir");
		free(path);
		exit(EXIT_FAILURE);
	}

	free(path);
}

/* Delete all configs. */
static void emu_bulk_purge_configs(void)
{
	char *path, *p;
        DIR *dir;
	size_t sz, tsz;
	struct dirent *den;

	path = emu_get_directory();
	if (path == NULL)
		exit(EXIT_FAILURE);

        dir = opendir(path);
	if (dir == NULL) {
	        fputs("emubox: missing config directory.\n",
		      stderr);
	        free(path);
		exit(EXIT_FAILURE);
	}

	sz = (size_t)0;
	/* Size of /home/<user>/.emubox */
        tsz = strlen(path);
	while ((den = readdir(dir)) != NULL) {
		if (strcmp(den->d_name, ".") == 0 ||
		    strcmp(den->d_name, "..") == 0)
			continue;

		if (den->d_type == DT_REG) {
		        sz = strlen(den->d_name) + tsz + (size_t)4;
			p = calloc(sz, sizeof(char));
			if (p == NULL) {
				closedir(dir);
				free(path);
			        err(EXIT_FAILURE, "calloc");
			}
			snprintf(p, sz, "%s/%s", path, den->d_name);
		        fprintf(stdout, "emubox: deleted: %s\n", p);
			if (unlink(p) == -1)
				/* We can ignore other things here. */
				warn("unlink");
			free(p);
		}
	}

	/* We didn't purge anything. Assuming there's
	   nothing to purge. */
	if (sz == (size_t)0)
		fputs("emubox: "
		      "no config files are present to purge.\n",
		      stderr);

	closedir(dir);
        free(path);
}

/* Delete a single config. */
static void emu_purge_config(const char *name)
{
	char *path, *p;
	size_t sz, lsz;
	struct stat st;

	path = emu_get_directory();
	if (path == NULL)
		exit(EXIT_FAILURE);

	sz = strlen(path) + strlen(name) + (size_t)7;
	p = calloc(sz, sizeof(char));
	if (p == NULL) {
		free(path);
	        err(EXIT_FAILURE, "calloc");
	}

	snprintf(p, sz, "%s/%s", path, name);

	/* Check whether the name actually has
	   a ".cfg" file extension. We can't do
	   that for entire buffer as there might
	   be users' whose name also contains ".cfg". */
	if (strstr(name, ".cfg") == NULL) {
	        lsz = strlen(p);
		memcpy(p + lsz, ".cfg", (size_t)4);
		if (stat(p, &st) == -1)
			if (errno == ENOENT)
				p[lsz] = '\0';
	}

	if (unlink(p) == -1) {
		if (errno == ENOENT)
			fprintf(stderr,
				"emubox: unknown config file: %s\n",
				name);
		else
			warn("unlink");

		exit(EXIT_FAILURE);
	}

	fprintf(stdout, "emubox: deleted config: %s\n",
		emu_basename(p));
	free(p);
	free(path);
}

/* Create a new emubox config. */
static void emu_create_new(const char *name)
{
	int fd;
	size_t sz;
	char *path, *fmt, *p;
	struct stat st;

	path = emu_get_directory();
	if (path == NULL)
		exit(EXIT_FAILURE);

	sz = strlen(path) + strlen(name) + (size_t)10;
	p = calloc(sz, sizeof(char));
	if (p == NULL) {
		free(path);
	        err(EXIT_FAILURE, "calloc");
	}

	if (strstr(name, ".cfg") == NULL)
		fmt = "%s/%s.cfg";
	else
		fmt = "%s/%s";

	snprintf(p, sz, fmt, path, name);

	/* Check whether the file already exists. */
	if (stat(p, &st) == 0) {
	        fprintf(stderr,
			"emubox: file \"%s\" already exists.\n",
			emu_basename(p));
		goto out_last_cleanup;
	}

	/* If the file doesn't exist, create one. */
	fd = open(p, O_CREAT, S_IRWXU);
	if (fd == -1) {
	        warn("open");
		goto out_last_cleanup;
	}

	fprintf(stdout,
		"emubox: done: created \"%s\".\n",
		emu_basename(p));
	close(fd);

out_last_cleanup:
	free(p);
	free(path);
}

/* Show the usage. */
static void usage(int status)
{
	fputs(
		"emubox\n"
		"   --init\t- Initialize emubox directory\n"
		"   --new\t- Create one or more new configuration file(s)\n"
		"   --delete\t- Delete one or more existing configuration file(s)\n"
		"   --purge\t- Purge all configuration file(s)\n"
		"   --select\t- Select a configuration from a ncurses driven menu\n"
		"   --settings\t- Open 86box settings panel\n"
		"   --fullscreen\t- Enable fullscreen before launching 86box\n"
		"   --fsr\t- Alias of --fullscreen\n"
		"   --language\t- Set a language before launching 86box\n"
		"   --help\t- Show this menu\n",
		status == EXIT_FAILURE ? stderr : stdout);
	exit(status);
}

int main(int argc, char **argv)
{
	int opt, i;
	char *lang;
	struct option long_options[] = {
		{ "init",        no_argument,        NULL, OPT_INIT },
		{ "new",         required_argument,  NULL, OPT_NEW },
		{ "delete",      required_argument,  NULL, OPT_DELETE },
		{ "purge",       no_argument,        NULL, OPT_PURGE },
		{ "select",      no_argument,        NULL, OPT_SELECT },
		{ "settings",    no_argument,        NULL, OPT_SETTINGS },
		{ "fsr",         no_argument,        NULL, OPT_FULLSCREEN },
		{ "fullscreen",  no_argument,        NULL, OPT_FULLSCREEN },
		{ "language",    required_argument,  NULL, OPT_LANGUAGE },
		{ "help",        no_argument,        NULL, OPT_HELP },
		{ NULL,          0,                  NULL, 0 },
	};
	struct emubox_opts opts = {0};

	if (argc < 2 || argv[1][0] != '-' ||
	    strcmp(argv[1], "-") == 0 ||
	    strcmp(argv[1], "--") == 0)
		usage(EXIT_FAILURE);

	/* Set no default locale. */
	setlocale(LC_ALL, "");
	/* Check if 86box exists in specified path. */
	emu_is_86box();

	lang = NULL;
        while ((opt = getopt_long(
			argc, argv, "", long_options, NULL)) != -1) {

		switch (opt) {
		case OPT_INIT:
		        opts.init_opt = 1;
		        break;

		case OPT_NEW:
			opts.new_opt = 1;
			break;

		case OPT_DELETE:
			opts.delete_opt = 1;
		        break;

		case OPT_PURGE:
			opts.purge_opt = 1;
		        break;

		case OPT_SELECT:
		        opts.select_opt = 1;
			break;

		case OPT_SETTINGS:
			opts.settings_opt = 1;
			break;

		case OPT_FULLSCREEN:
			opts.fullscreen_opt = 1;
			break;

		case OPT_LANGUAGE:
			lang = optarg;
			break;

		case OPT_HELP:
			usage(EXIT_SUCCESS);
			/* FALLTHROUGH */

		default:
			usage(EXIT_FAILURE);
		}
	}

	optind--;
	argc -= optind;
	argv += optind;

	/* --init */
	if (opts.init_opt) {
	        emu_init_directory();
	        goto out_ok;
	}

	/* --new */
	if (opts.new_opt) {
		for (i = 0; i < argc; i++) {
			switch (argv[i][0]) {
			case '-':
			case '/':
			case '\\':
				fputs("emubox: "
				      "an unexpected character was passed."
				      " Ignored.\n",
				      stderr);
				continue;

			default:
				emu_create_new(argv[i]);
			}
	        }
		goto out_ok;
	}

	/* --delete */
	if (opts.delete_opt) {
		for (i = 0; i < argc; i++) {
			switch (argv[i][0]) {
			case '-':
			case '/':
			case '\\':
				continue;

			default:
				emu_purge_config(argv[i]);
			}
		}
		goto out_ok;
	}

	/* --purge */
	if (opts.purge_opt) {
		emu_bulk_purge_configs();
		goto out_ok;
	}

	/* --select */
	if (opts.select_opt) {
	        emu_select_list(lang ? lang : NULL, opts.fullscreen_opt, 0);
		goto out_ok;
	}

	/* --settings */
	if (opts.settings_opt) {
		/* All other arguments, except settings, are ignored here. */
		emu_select_list(NULL, 0, opts.settings_opt);
		goto out_ok;
	}

out_ok:
        exit(EXIT_SUCCESS);
}
