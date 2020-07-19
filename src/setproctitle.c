/* ==========================================================================
 * setproctitle.c - Linux/Darwin setproctitle.
 * --------------------------------------------------------------------------
 * Copyright (C) 2010  William Ahern
 * Copyright (C) 2013  Salvatore Sanfilippo
 * Copyright (C) 2013  Stam He
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 * NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 * ==========================================================================
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stddef.h>	/* NULL size_t */
#include <stdarg.h>	/* va_list va_start va_end */
#include <stdlib.h>	/* malloc(3) setenv(3) clearenv(3) setproctitle(3) getprogname(3) */
#include <stdio.h>	/* vsnprintf(3) snprintf(3) */

#include <string.h>	/* strlen(3) strchr(3) strdup(3) memset(3) memcpy(3) */

#include <errno.h>	/* errno program_invocation_name program_invocation_short_name */

#if !defined(HAVE_SETPROCTITLE)
#if (defined __NetBSD__ || defined __FreeBSD__ || defined __OpenBSD__ || defined __DragonFly__)
#define HAVE_SETPROCTITLE 1
#else
#define HAVE_SETPROCTITLE 0
#endif
#endif


#if !HAVE_SETPROCTITLE
#if (defined __linux || defined __APPLE__)

extern char **environ;

static struct {
	/* original value */
	const char *arg0;

	/* title space available */
	char *base, *end;

	 /* pointer to original nul character within base */
	char *nul;

	_Bool reset;
	int error;
} SPT;


#ifndef SPT_MIN
#define SPT_MIN(a, b) (((a) < (b))? (a) : (b))
#endif

static inline size_t spt_min(size_t a, size_t b) {
	return SPT_MIN(a, b);
} /* spt_min() */


/*
 * For discussion on the portability of the various methods, see
 * http://lists.freebsd.org/pipermail/freebsd-stable/2008-June/043136.html
 */
static int spt_clearenv(void) {
#if __GLIBC__
	clearenv();

	return 0;
#else
	extern char **environ;
	static char **tmp;

	if (!(tmp = malloc(sizeof *tmp)))
		return errno;

	tmp[0]  = NULL;
	environ = tmp;

	return 0;
#endif
} /* spt_clearenv() */


static int spt_copyenv(char *oldenv[]) {
	extern char **environ;
	char *eq;
	int i, error;
    /// 如果environ != oldenv则说明environ已经被设置过了
	if (environ != oldenv)
		return 0;
    /// 让老的environ失效,但是不释放那片内存
	if ((error = spt_clearenv()))
		goto error;
    /// setenv会生成新的environ, 不必手动设置新environ的内存
	for (i = 0; oldenv[i]; i++) {
		if (!(eq = strchr(oldenv[i], '=')))
			continue;

		*eq = '\0';
		error = (0 != setenv(oldenv[i], eq + 1, 1))? errno : 0;
		*eq = '=';

		if (error)
			goto error;
	}

	return 0;
error:
	environ = oldenv; /// 如果设置过程中出问题了, 则还原environ的设置

	return error;
} /* spt_copyenv() */


static int spt_copyargs(int argc, char *argv[]) {
	char *tmp;
	int i;
    /// 除argv[0]以外的所有argv都由strdup重新生成 内存不再连续
	for (i = 1; i < argc || (i >= argc && argv[i]); i++) {
		if (!argv[i])
			continue;

		if (!(tmp = strdup(argv[i])))
			return errno;

		argv[i] = tmp;
	}

	return 0;
} /* spt_copyargs() */

/// argv 与 environ 是连续的内存空间。argv[0]里面对应的就是进程名，如直接更改argv[0] 如名字变长会覆盖后面内存值所以需要本函数操作
void spt_init(int argc, char *argv[]) {
        /// macOS和Linux系统创建进程后会给进程分配一个全局的environment环境变量char ** environ, 它是一个char*数组, 里面保存的是类似{k=v, k=v, k=v}这样的字符串数组
        char **envp = environ;
	char *base, *end, *nul, *tmp;
	int i, error;

	if (!(base = argv[0])) /// base指向进程名字开始位置  如为null 直接返回
		return;

	nul = &base[strlen(base)]; /// 指向进程名字符串的结束位置(不包括\0)
	end = nul + 1;

	/// 虽然argv和environ是连续的内存, 但是其中包含\0,
    /// 而且不知道environ的长度,所以得遍历二者, 将这片内存退化成char*, 也就是base
    /// todo 为啥遍历一遍就可以退化? 仅仅找到了end位置而已?
	for (i = 0; i < argc || (i >= argc && argv[i]); i++) {
		if (!argv[i] || argv[i] < end) /// 为null 或 argv[0] 不处理
			continue;

		end = argv[i] + strlen(argv[i]) + 1;
	}

	for (i = 0; envp[i]; i++) {
		if (envp[i] < end)
			continue;

		end = envp[i] + strlen(envp[i]) + 1;
	}
    /// 原进程名备份到全局结构体中  字符串深拷贝
	if (!(SPT.arg0 = strdup(argv[0])))
		goto syerr;

#if __GLIBC__
	if (!(tmp = strdup(program_invocation_name)))
		goto syerr;

	program_invocation_name = tmp;

	if (!(tmp = strdup(program_invocation_short_name)))
		goto syerr;

	program_invocation_short_name = tmp;
#elif __APPLE__
	if (!(tmp = strdup(getprogname())))
		goto syerr;

	setprogname(tmp);
#endif

    /// 在新的内存区域产生新的environ, 原来的environ内存区域将归base所有
	if ((error = spt_copyenv(envp)))
		goto error;
    /// 重新设置了除argv[0]以外的所有argv, 将在新的内存区域产生新的除argv[0]以外的argv, 原来的argv[1-n]内存区域将归base所有
	if ((error = spt_copyargs(argc, argv)))
		goto error;
    /// 原来argv environ共有的那片连续内存全部被转换成base, 即char*, 也是argv[0], 也就是进程名。argv和environ将不再连续
    /// 新的内容全部由strdup生成, 修改进程名, 只需要改SPT->base就可以了。
	SPT.nul  = nul;
	SPT.base = base;
	SPT.end  = end;

	return;
syerr:
	error = errno;
error:
	SPT.error = error;
} /* spt_init() */


#ifndef SPT_MAXTITLE
#define SPT_MAXTITLE 255
#endif

void setproctitle(const char *fmt, ...) {
	char buf[SPT_MAXTITLE + 1]; /* use buffer in case argv[0] is passed */
	va_list ap;
	char *nul;
	int len, error;

	if (!SPT.base)
		return;

	if (fmt) {
		va_start(ap, fmt);
		len = vsnprintf(buf, sizeof buf, fmt, ap);
		va_end(ap);
	} else {
		len = snprintf(buf, sizeof buf, "%s", SPT.arg0);
	}

	if (len <= 0)
		{ error = errno; goto error; }

	if (!SPT.reset) {
		memset(SPT.base, 0, SPT.end - SPT.base);
		SPT.reset = 1;
	} else {
		memset(SPT.base, 0, spt_min(sizeof buf, SPT.end - SPT.base));
	}

	len = spt_min(len, spt_min(sizeof buf, SPT.end - SPT.base) - 1);
	memcpy(SPT.base, buf, len);
	nul = &SPT.base[len];

	if (nul < SPT.nul) {
		*SPT.nul = '.';
	} else if (nul == SPT.nul && &nul[1] < SPT.end) {
		*SPT.nul = ' ';
		*++nul = '\0';
	}

	return;
error:
	SPT.error = error;
} /* setproctitle() */


#endif /* __linux || __APPLE__ */
#endif /* !HAVE_SETPROCTITLE */
