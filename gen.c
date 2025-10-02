// See UNLICENSE file for copyright and license details.

#include <dirent.h>
#include <fcntl.h>
#include <err.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DICTIONARY_PATH "dictionary"

// buffer sizes for individual words and files for words, might wanna adjust this if things get bigger
#define WORDBUFSIZE 32
#define FILEBUFSIZE 2048

// pre-allocated buffer sizes for the 4 lists, will be realloc'd by _INCR more bytes if there's not enough space
#define WORDLIST_PREALLOC 128
#define WORDLIST_INCR 128

#define DESCLIST_PREALLOC 8192
#define DESCLIST_INCR 4096

#define DEFLIST_PREALLOC 8192
#define DEFLIST_INCR 4096

#define TAGLIST_PREALLOC 8192
#define TAGLIST_INCR 4096

__attribute__((returns_nonnull))
static void *
emalloc(size_t size)
{
	void *rv = malloc(size);
	if (!rv)
		err(1, "malloc");
	return rv;
}

__attribute__((returns_nonnull))
static void *
erealloc(void *ptr, size_t size)
{
	void *rv = realloc(ptr, size);
	if (!rv)
		err(1, "realloc");
	return rv;
}

static unsigned char *
escquot(unsigned char *restrict dst, const unsigned char *restrict src, size_t n)
{
	const unsigned char *const end = src + n;
	const unsigned char *p = src;
	unsigned char *w = dst;

	const unsigned char *q;
	/* register */ size_t rem;

	for (;;) {
		rem = end - p;
		q = memchr(p, '"', rem);
		if (!q) {
			w = mempcpy(w, p, rem);
			break;
		}
		rem = q - p;
		w = mempcpy(w, p, rem);
		*w++ = '\\';
		*w++ = '"';
		p = q + 1;
	}
	return w;
}

static int
strcoll_wrap(const void *a, const void *b)
{
	return strcoll(*(const char **)a, *(const char **)b);
}

static void
output_wordlist(char *const *wl, size_t sz)
{
	fputs("const wordlist = [", stdout);
	for (size_t i = 0; i < sz - 1; i++)
		printf("\"%s\",", wl[i]);
	printf("\"%s\"];\n", wl[sz - 1]);
}

static void
boundscheck(size_t n, size_t *bufsz, size_t szincr, char **buf, char **bufp)
{
	if (*bufp + n > *buf + *bufsz) {
		size_t poff = *bufp - *buf;
		*bufsz += szincr;
		*buf = erealloc(*buf, *bufsz);
		*bufp = *buf + poff;
	}
}

static void
output_everything_else(char *const *wl, size_t sz)
{
	size_t desclistsz = DESCLIST_PREALLOC;
	char *desclist = emalloc(desclistsz);
	char *descp = desclist;

	size_t deflistsz = DEFLIST_PREALLOC;
	char *deflist = emalloc(deflistsz);
	char *defp = deflist;

	size_t taglistsz = TAGLIST_PREALLOC;
	char *taglist = emalloc(taglistsz);
	char *tagp = taglist;

	int dirfd = open(DICTIONARY_PATH, O_RDONLY);
	if (dirfd < 0)
		err(1, "open " DICTIONARY_PATH);

	int fd;
	char *buf = emalloc(FILEBUFSIZE);
	char *line;
	char *p, *p2;
	ssize_t nread;
	for (size_t i = 0; i < sz; i++) {
		if ((fd = openat(dirfd, wl[i], O_RDONLY)) < 0)
			err(1, "open " DICTIONARY_PATH "/%s", wl[i]);
		if ((nread = read(fd, buf, FILEBUFSIZE - 1)) < 0)
			err(1, "read " DICTIONARY_PATH "/%s", wl[i]);
		buf[nread] = '\0';
		line = buf;

		while (*line != '\0') {
			// definition
			if (!(p = strchr(line, '|')))
				errx(1, "invalid data in file " DICTIONARY_PATH "/%s: was expecting | character!", wl[i]);
			*p = '\0';
			boundscheck((p - line) + 3, &deflistsz, DEFLIST_INCR, &deflist, &defp);
			defp = mempcpy(escquot(defp, line, p - line), "\",\"", 3);

			// descriptor
			p2 = p + 1;
			if (!(p = strchr(p2, '|')))
				errx(1, "invalid data in file " DICTIONARY_PATH "/%s: was expecting | character!", wl[i]);
			*p = '\0';
			boundscheck((p - p2) + 3, &desclistsz, DESCLIST_INCR, &desclist, &descp);
			descp = mempcpy(escquot(descp, p2, p - p2), "\",\"", 3);

			// tags
			p2 = ++p;
			if (!(line = strchr(p2, '\n')))
				errx(1, "invalid data in file " DICTIONARY_PATH "/%s: was expecting newline!", wl[i]);
			*line = '\0';
			while ((p = strchr(p, ' ')) != NULL) {
				*p = '\0';
				boundscheck((p - p2) + 3, &taglistsz, TAGLIST_INCR, &taglist, &tagp);
				tagp = mempcpy(mempcpy(tagp, p2, p - p2), "\",\"", 3);
				p2 = ++p;
			}
			boundscheck((line - p2) + 5, &taglistsz, TAGLIST_INCR, &taglist, &tagp);
			tagp = mempcpy(mempcpy(tagp, p2, line - p2), "\"],[\"", 5);
			line++;
		}
		// replace final ," with ],[" and ,[" with ],[["
		defp += 2;
		descp += 2;
		tagp += 2;
		boundscheck(2, &deflistsz, 2, &deflist, &defp);
		memcpy(defp - 4, "],[\"", 4);
		boundscheck(2, &desclistsz, 2, &desclist, &descp);
		memcpy(descp - 4, "],[\"", 4);
		boundscheck(2, &taglistsz, 2, &taglist, &tagp);
		memcpy(tagp - 5, "],[[\"", 5);
		close(fd);
	}
	close(dirfd);

	// replace ,[" or ,[[" with ]
	defp -= 2;
	descp -= 2;
	tagp -= 3;
	*(defp - 1) = *(descp - 1) = *(tagp - 1) = ']';

	*defp = *descp = *tagp = '\0';
	printf("const desclist = [[\"%s;\nconst deflist = [[\"%s;\nconst taglist = [[[\"%s;\n", desclist, deflist, taglist);
	free(buf);
	free(taglist);
	free(deflist);
	free(desclist);
}

int
main(int argc, char *argv[])
{
	DIR *dp = opendir(DICTIONARY_PATH);
	if (!dp)
		err(1, "opendir " DICTIONARY_PATH);

	char **wordlist = emalloc(WORDLIST_PREALLOC * sizeof(char *));
	size_t idx = 0;

	struct dirent *d;
	size_t l;
	while ((d = readdir(dp))) {
		if (d->d_name[0] == '.' && (d->d_name[1] == '.' || d->d_name[1] == '\0'))
			continue;
		wordlist[idx] = emalloc(WORDBUFSIZE);
		l = strlen(d->d_name);
		if (!memchr(d->d_name, '"', l))
			memcpy(wordlist[idx], d->d_name, l + 1);
		else
			escquot(wordlist[idx], d->d_name, l + 1); // !! buffer overflow risk
		idx++;
	}

	closedir(dp);

	setlocale(LC_COLLATE, "en_US.UTF-8");
	qsort(wordlist, idx, sizeof(char *), strcoll_wrap);

	output_wordlist(wordlist, idx);
	output_everything_else(wordlist, idx);

	for (size_t i = 0; i < idx; i++)
		free(wordlist[i]);
	free(wordlist);

	return 0;
}
