// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 mijsys

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

enum capture_mode {
	MODE_FULL,
	MODE_AREA,
	MODE_FULL_COPY,
	MODE_AREA_COPY,
};

struct screenshot_paths {
	char runtime_dir[PATH_MAX];
	char fifo_path[PATH_MAX];
	char pid_path[PATH_MAX];
	char shot_dir[PATH_MAX];
};

static volatile sig_atomic_t stop_requested;
static struct screenshot_paths g_paths;

static void
print_usage(FILE *out)
{
	fprintf(out, "Usage: karton-screenshot [--daemon|full|area|full-copy|area-copy]\n");
}

static bool
string_is_empty(const char *s)
{
	return !s || !s[0];
}

static bool
is_number(const char *s)
{
	if (string_is_empty(s)) {
		return false;
	}
	for (size_t i = 0; s[i]; i++) {
		if (!isdigit((unsigned char)s[i])) {
			return false;
		}
	}
	return true;
}

static bool
command_exists(const char *cmd)
{
	if (string_is_empty(cmd)) {
		return false;
	}
	if (strchr(cmd, '/')) {
		return access(cmd, X_OK) == 0;
	}

	const char *path_env = getenv("PATH");
	if (string_is_empty(path_env)) {
		path_env = "/usr/local/bin:/usr/bin:/bin";
	}

	char *paths = strdup(path_env);
	if (!paths) {
		return false;
	}

	bool found = false;
	char *saveptr = NULL;
	for (char *seg = strtok_r(paths, ":", &saveptr); seg; seg = strtok_r(NULL, ":", &saveptr)) {
		char candidate[PATH_MAX] = { 0 };
		if (snprintf(candidate, sizeof(candidate), "%s/%s", seg, cmd) >= (int)sizeof(candidate)) {
			continue;
		}
		if (access(candidate, X_OK) == 0) {
			found = true;
			break;
		}
	}

	free(paths);
	return found;
}

static bool
join_path2(char *out, size_t out_size, const char *a, const char *b)
{
	if (!out || out_size == 0 || string_is_empty(a) || string_is_empty(b)) {
		return false;
	}

	size_t la = strlen(a);
	size_t lb = strlen(b);
	bool need_sep = a[la - 1] != '/';
	size_t need = la + (need_sep ? 1 : 0) + lb + 1;
	if (need > out_size) {
		out[0] = '\0';
		return false;
	}

	memcpy(out, a, la);
	size_t off = la;
	if (need_sep) {
		out[off++] = '/';
	}
	memcpy(out + off, b, lb);
	out[off + lb] = '\0';
	return true;
}

static int
run_process(char *const argv[], const char *stdin_file)
{
	pid_t pid = fork();
	if (pid < 0) {
		return errno ? errno : 1;
	}
	if (pid == 0) {
		if (stdin_file) {
			int fd = open(stdin_file, O_RDONLY);
			if (fd < 0) {
				_exit(125);
			}
			if (dup2(fd, STDIN_FILENO) < 0) {
				close(fd);
				_exit(125);
			}
			close(fd);
		}
		execvp(argv[0], argv);
		_exit(127);
	}

	int status = 0;
	if (waitpid(pid, &status, 0) < 0) {
		return errno ? errno : 1;
	}
	if (WIFEXITED(status)) {
		return WEXITSTATUS(status);
	}
	if (WIFSIGNALED(status)) {
		return 128 + WTERMSIG(status);
	}
	return 1;
}

static int
mkdir_p(const char *path)
{
	if (string_is_empty(path)) {
		return EINVAL;
	}

	char tmp[PATH_MAX] = { 0 };
	if (snprintf(tmp, sizeof(tmp), "%s", path) >= (int)sizeof(tmp)) {
		return ENAMETOOLONG;
	}

	size_t len = strlen(tmp);
	if (len == 0) {
		return EINVAL;
	}
	if (tmp[len - 1] == '/') {
		tmp[len - 1] = '\0';
	}

	for (char *p = tmp + 1; *p; p++) {
		if (*p != '/') {
			continue;
		}
		*p = '\0';
		if (mkdir(tmp, 0755) < 0 && errno != EEXIST) {
			return errno;
		}
		*p = '/';
	}

	if (mkdir(tmp, 0755) < 0 && errno != EEXIST) {
		return errno;
	}
	return 0;
}

static int
build_output_path(const struct screenshot_paths *paths, char *out, size_t out_size)
{
	time_t now = time(NULL);
	struct tm tm_now = { 0 };
	if (now == (time_t)-1 || !localtime_r(&now, &tm_now)) {
		return 1;
	}

	char ts[64] = { 0 };
	if (strftime(ts, sizeof(ts), "%Y-%m-%d_%H-%M-%S", &tm_now) == 0) {
		return 1;
	}

	if (snprintf(out, out_size, "%s/karton-%s.png", paths->shot_dir, ts) >= (int)out_size) {
		return ENAMETOOLONG;
	}
	return 0;
}

static int
notify_send(const char *title, const char *body)
{
	if (!command_exists("notify-send")) {
		return 0;
	}
	char *const argv[] = {
		"notify-send",
		(char *)title,
		(char *)body,
		NULL,
	};
	return run_process(argv, NULL);
}

static int
run_grim_full(const char *output)
{
	if (!command_exists("grim")) {
		fprintf(stderr, "karton-screenshot: missing command grim\n");
		return 127;
	}

	char *const argv[] = {
		"grim",
		(char *)output,
		NULL,
	};
	return run_process(argv, NULL);
}

static int
run_slurp(char *geom, size_t geom_size)
{
	if (!command_exists("slurp")) {
		fprintf(stderr, "karton-screenshot: missing command slurp\n");
		return 127;
	}

	FILE *pipe = popen("slurp 2>/dev/null", "r");
	if (!pipe) {
		return 1;
	}

	if (!fgets(geom, (int)geom_size, pipe)) {
		int rc = pclose(pipe);
		if (rc == -1) {
			return 1;
		}
		return 130;
	}

	int rc = pclose(pipe);
	if (rc == -1) {
		return 1;
	}
	if (WIFEXITED(rc) && WEXITSTATUS(rc) != 0) {
		return WEXITSTATUS(rc);
	}

	size_t len = strlen(geom);
	while (len > 0 && (geom[len - 1] == '\n' || geom[len - 1] == '\r')) {
		geom[--len] = '\0';
	}
	if (len == 0) {
		return 130;
	}
	return 0;
}

static int
run_grim_area(const char *output)
{
	char geom[256] = { 0 };
	int rc = run_slurp(geom, sizeof(geom));
	if (rc != 0) {
		return rc;
	}

	char *const argv[] = {
		"grim",
		"-g",
		geom,
		(char *)output,
		NULL,
	};
	return run_process(argv, NULL);
}

static int
copy_to_clipboard(const char *file)
{
	if (!command_exists("wl-copy")) {
		fprintf(stderr, "karton-screenshot: wl-copy not found, screenshot saved only\n");
		return 127;
	}

	char *const argv[] = {
		"wl-copy",
		NULL,
	};
	return run_process(argv, file);
}

static int
parse_mode(const char *arg, enum capture_mode *mode)
{
	if (string_is_empty(arg) || !strcmp(arg, "full")) {
		*mode = MODE_FULL;
		return 0;
	}
	if (!strcmp(arg, "area")) {
		*mode = MODE_AREA;
		return 0;
	}
	if (!strcmp(arg, "full-copy")) {
		*mode = MODE_FULL_COPY;
		return 0;
	}
	if (!strcmp(arg, "area-copy")) {
		*mode = MODE_AREA_COPY;
		return 0;
	}
	return 2;
}

static int
capture_mode_to_file(const struct screenshot_paths *paths, enum capture_mode mode, char *out_path,
	size_t out_path_size)
{
	int rc = mkdir_p(paths->shot_dir);
	if (rc != 0) {
		fprintf(stderr, "karton-screenshot: cannot create screenshot dir %s: %s\n",
			paths->shot_dir, strerror(rc));
		return rc;
	}

	rc = build_output_path(paths, out_path, out_path_size);
	if (rc != 0) {
		return rc;
	}

	switch (mode) {
	case MODE_FULL:
	case MODE_FULL_COPY:
		rc = run_grim_full(out_path);
		break;
	case MODE_AREA:
	case MODE_AREA_COPY:
		rc = run_grim_area(out_path);
		break;
	}
	if (rc != 0) {
		return rc;
	}

	if (mode == MODE_FULL_COPY || mode == MODE_AREA_COPY) {
		(void)copy_to_clipboard(out_path);
	}

	if (mode == MODE_FULL_COPY || mode == MODE_AREA_COPY) {
		(void)notify_send("Screenshot copied", out_path);
	} else {
		(void)notify_send("Screenshot saved", out_path);
	}

	return 0;
}

static bool
is_own_daemon_process(pid_t pid)
{
	if (pid <= 1) {
		return false;
	}
	if (kill(pid, 0) < 0) {
		return false;
	}

	char path[64] = { 0 };
	snprintf(path, sizeof(path), "/proc/%ld/cmdline", (long)pid);
	FILE *f = fopen(path, "rb");
	if (!f) {
		return false;
	}

	char buf[1024] = { 0 };
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	if (n == 0) {
		return false;
	}

	for (size_t i = 0; i < n; i++) {
		if (buf[i] == '\0') {
			buf[i] = ' ';
		}
	}

	return strstr(buf, "karton-screenshot") && strstr(buf, "--daemon");
}

static int
read_pid_file(const char *pid_path, pid_t *pid)
{
	FILE *f = fopen(pid_path, "r");
	if (!f) {
		return errno;
	}

	char line[64] = { 0 };
	if (!fgets(line, sizeof(line), f)) {
		fclose(f);
		return EINVAL;
	}
	fclose(f);

	char *nl = strchr(line, '\n');
	if (nl) {
		*nl = '\0';
	}
	if (!is_number(line)) {
		return EINVAL;
	}

	long v = strtol(line, NULL, 10);
	if (v <= 1) {
		return EINVAL;
	}
	*pid = (pid_t)v;
	return 0;
}

static int
send_mode_to_daemon(const struct screenshot_paths *paths, const char *mode)
{
	pid_t pid = 0;
	if (read_pid_file(paths->pid_path, &pid) != 0) {
		return 1;
	}
	if (!is_own_daemon_process(pid)) {
		return 1;
	}

	struct stat st = { 0 };
	if (stat(paths->fifo_path, &st) < 0 || !S_ISFIFO(st.st_mode)) {
		return 1;
	}

	int fd = open(paths->fifo_path, O_WRONLY | O_NONBLOCK);
	if (fd < 0) {
		return 1;
	}

	dprintf(fd, "%s\n", mode);
	close(fd);
	return 0;
}

static void
cleanup_daemon_files(void)
{
	unlink(g_paths.pid_path);
	unlink(g_paths.fifo_path);
}

static void
signal_handler(int sig)
{
	(void)sig;
	stop_requested = 1;
}

static int
run_single_request(const struct screenshot_paths *paths, const char *mode, bool print_path)
{
	enum capture_mode parsed = MODE_FULL;
	int rc = parse_mode(mode, &parsed);
	if (rc != 0) {
		fprintf(stderr, "karton-screenshot: invalid mode\n");
		print_usage(stderr);
		return rc;
	}

	char out[PATH_MAX] = { 0 };
	rc = capture_mode_to_file(paths, parsed, out, sizeof(out));
	if (rc == 130) {
		return 130;
	}
	if (rc != 0) {
		return rc;
	}

	if (print_path) {
		printf("%s\n", out);
	}
	return 0;
}

static int
write_pid_file(const char *pid_path)
{
	FILE *f = fopen(pid_path, "w");
	if (!f) {
		return errno;
	}
	fprintf(f, "%ld\n", (long)getpid());
	fclose(f);
	return 0;
}

static int
run_daemon(struct screenshot_paths *paths)
{
	pid_t old_pid = 0;
	if (read_pid_file(paths->pid_path, &old_pid) == 0 && is_own_daemon_process(old_pid)) {
		return 0;
	}

	int rc = write_pid_file(paths->pid_path);
	if (rc != 0) {
		fprintf(stderr, "karton-screenshot: cannot write pid file %s: %s\n", paths->pid_path,
			strerror(rc));
		return rc;
	}

	unlink(paths->fifo_path);
	if (mkfifo(paths->fifo_path, 0600) < 0) {
		rc = errno;
		fprintf(stderr, "karton-screenshot: cannot create fifo %s: %s\n", paths->fifo_path,
			strerror(rc));
		unlink(paths->pid_path);
		return rc;
	}

	atexit(cleanup_daemon_files);
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	while (!stop_requested) {
		int fd = open(paths->fifo_path, O_RDWR);
		if (fd < 0) {
			if (errno == EINTR) {
				continue;
			}
			return errno;
		}

		FILE *fifo = fdopen(fd, "r");
		if (!fifo) {
			close(fd);
			return errno;
		}

		char line[64] = { 0 };
		while (!stop_requested && fgets(line, sizeof(line), fifo)) {
			char *nl = strpbrk(line, "\r\n");
			if (nl) {
				*nl = '\0';
			}
			if (line[0] == '\0') {
				continue;
			}
			(void)run_single_request(paths, line, false);
		}

		fclose(fifo);
	}

	return 0;
}

static void
init_paths(struct screenshot_paths *paths)
{
	memset(paths, 0, sizeof(*paths));

	const char *runtime = getenv("XDG_RUNTIME_DIR");
	if (string_is_empty(runtime)) {
		runtime = "/tmp";
	}
	snprintf(paths->runtime_dir, sizeof(paths->runtime_dir), "%s", runtime);
	if (!join_path2(paths->fifo_path, sizeof(paths->fifo_path), paths->runtime_dir,
		"karton-screenshot.fifo")) {
		paths->fifo_path[0] = '\0';
	}
	if (!join_path2(paths->pid_path, sizeof(paths->pid_path), paths->runtime_dir,
		"karton-screenshot.pid")) {
		paths->pid_path[0] = '\0';
	}

	const char *pictures = getenv("XDG_PICTURES_DIR");
	if (string_is_empty(pictures)) {
		const char *home = getenv("HOME");
		if (string_is_empty(home)) {
			home = "/tmp";
		}
		snprintf(paths->shot_dir, sizeof(paths->shot_dir), "%s/Pictures/Screenshots", home);
	} else {
		snprintf(paths->shot_dir, sizeof(paths->shot_dir), "%s/Screenshots", pictures);
	}
}

int
main(int argc, char **argv)
{
	init_paths(&g_paths);

	if (mkdir_p(g_paths.runtime_dir) != 0) {
		fprintf(stderr, "karton-screenshot: cannot create runtime dir %s\n", g_paths.runtime_dir);
		return 1;
	}

	if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		print_usage(stdout);
		return 0;
	}

	if (argc > 1 && !strcmp(argv[1], "--daemon")) {
		return run_daemon(&g_paths);
	}

	const char *mode = argc > 1 ? argv[1] : "full";
	if (send_mode_to_daemon(&g_paths, mode) == 0) {
		return 0;
	}

	return run_single_request(&g_paths, mode, true);
}
