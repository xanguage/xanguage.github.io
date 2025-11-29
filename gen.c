// See UNLICENSE file for copyright and license details.

#include <dirent.h>
#include <fcntl.h>
#include <err.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __APPLE__
void *mempcpy(void *dest, void const *src, size_t len);
#endif

#define DICTIONARY_PATH "dictionary"

/* buffer sizes for individual words and files for words,
 * might wanna adjust this if things get bigger */
#define WORDBUFSIZE 32
#define FILEBUFSIZE 2048

/* pre-allocated buffer sizes for the 4 lists, 
 * will be realloc'd by _INCR more bytes if there's not enough space */
#define WORDLIST_PREALLOC 128
#define WORDLIST_INCR 128

#define ETYLIST_PREALLOC 16384
#define ETYLIST_INCR 8192

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

static inline size_t
countquot(const char *s, size_t l)
{
	size_t count = 0;
	for (size_t i = 0; i < l; i++)
		count += (s[i] == '"');
	return count;
}

static unsigned char *
escquot(unsigned char *restrict dst, const unsigned char *restrict src,
		size_t n)
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
		*bufsz += (szincr >= n) ? szincr : n;
		*buf = erealloc(*buf, *bufsz);
		*bufp = *buf + poff;
	}
}

static void
nextety(char **defp, char **descp, char **tagp,
	char **deflist, char **desclist, char **taglist,
	size_t *deflistsz, size_t *desclistsz, size_t *taglistsz)
{
	// replace final ," with ],[" and ,[" with ],[["
	(*defp) += 2;
	(*descp) += 2;
	(*tagp) += 2;
	boundscheck(2, deflistsz, 2, deflist, defp);
	memcpy((*defp) - 4, "],[\"", 4);
	boundscheck(2, desclistsz, 2, desclist, descp);
	memcpy((*descp) - 4, "],[\"", 4);
	boundscheck(2, taglistsz, 2, taglist, tagp);
	memcpy((*tagp) - 5, "],[[\"", 5);
}

static void
nextword(char **etyp, char **defp, char **descp, char **tagp,
	char **etylist, char **deflist, char **desclist, char **taglist,
	size_t *etylistsz, size_t *deflistsz, size_t *desclistsz, size_t *taglistsz)
{
	// replace final ," with ],["
	(*etyp) += 2;
	boundscheck(2, etylistsz, 2, etylist, etyp);
	memcpy((*etyp) - 4, "],[\"", 4);

	// replace final ,[" with "]],[["
	(*defp) += 4;
	(*descp) += 4;
	boundscheck(4, deflistsz, 4, deflist, defp);
	memcpy((*defp) - 7, "\"]],[[\"", 7);
	boundscheck(4, desclistsz, 4, desclist, descp);
	memcpy((*descp) - 7, "\"]],[[\"", 7);

	// replace final ,[[" with ]]],[[["
	(*tagp) += 4;
	boundscheck(4, taglistsz, 4, taglist, tagp);
	memcpy((*tagp) - 8, "]]],[[[\"", 8);
}

static void
end(char **etyp, char **defp, char **descp, char **tagp)
{
	// replace final ,[" ,[[" ,[[[" with ]
	(*etyp) -= 2;
	(*defp) -= 3;
	(*descp) -= 3;
	(*tagp) -= 4;
	*((*etyp) - 1) = *((*defp) - 1) = *((*descp) - 1) = *((*tagp) - 1) = ']';
	*(*etyp) = *(*defp) = *(*descp) = *(*tagp) = '\0';
}

static void
output_everything_else(char *const *wl, size_t sz)
{
	size_t etylistsz = ETYLIST_PREALLOC;
	char *etylist = emalloc(etylistsz);
	char *etyp = etylist;

	size_t deflistsz = DEFLIST_PREALLOC;
	char *deflist = emalloc(deflistsz);
	char *defp = deflist;

	size_t desclistsz = DESCLIST_PREALLOC;
	char *desclist = emalloc(desclistsz);
	char *descp = desclist;

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
	int newword;
	for (size_t i = 0; i < sz; i++) {
		if ((fd = openat(dirfd, wl[i], O_RDONLY)) < 0)
			err(1, "open " DICTIONARY_PATH "/%s", wl[i]);
		if ((nread = read(fd, buf, FILEBUFSIZE - 1)) < 0)
			err(1, "read " DICTIONARY_PATH "/%s", wl[i]);
		buf[nread] = '\0';
		line = buf;
		newword = 1;

		// ok yeah i give up trying to make this fit within 80 columns 
		/* how do you use the worst variable names imaginable and it's
		 * still too wide to fit on my screen */
		while (*line != '\0') {
			if (line[0] == '>') {
				if (!(p = strchr(line, '\n')))
					errx(1, "invalid data in file " DICTIONARY_PATH "/%s: was expecting newline!", wl[i]);
				*p = '\0';

				// - 1 to remove leading >
				size_t l = strlen(line) - 1;
				boundscheck(l + countquot(line + 1, l) + 3, &etylistsz, ETYLIST_INCR, &etylist, &etyp);
				etyp = mempcpy(escquot(etyp, line + 1, l), "\",\"", 3);

				if (!newword)
					nextety(&defp, &descp, &tagp,
						&deflist, &desclist, &taglist,
						&deflistsz, &desclistsz, &taglistsz);

				line = p + 1;
				continue;
			}
 
			// definition
			if (!(p = strchr(line, '|')))
				errx(1, "invalid data in file " DICTIONARY_PATH "/%s: was expecting | character!", wl[i]);
			*p = '\0';
			boundscheck((p - line) + countquot(line, p - line) + 3, &deflistsz, DEFLIST_INCR, &deflist, &defp);
			defp = mempcpy(escquot(defp, line, p - line), "\",\"", 3);

			// descriptor
			p2 = p + 1;
			if (!(p = strchr(p2, '|')))
				errx(1, "invalid data in file " DICTIONARY_PATH "/%s: was expecting | character!", wl[i]);
			*p = '\0';
			boundscheck((p - p2) + countquot(p2, p - p2) + 3, &desclistsz, DESCLIST_INCR, &desclist, &descp);
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
			newword = 0;
		}
		nextword(&etyp, &defp, &descp, &tagp,
			&etylist, &deflist, &desclist, &taglist,
			&etylistsz, &deflistsz, &desclistsz, &taglistsz);
		close(fd);
	}
	close(dirfd);

	end(&etyp, &defp, &descp, &tagp);

	printf("const etylist=[[\"%s;\nconst desclist = [[[\"%s;\nconst deflist = [[[\"%s;\nconst taglist = [[[[\"%s;\n", etylist, desclist, deflist, taglist);
	free(buf);
	free(taglist);
	free(desclist);
	free(deflist);
	free(etylist);
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
