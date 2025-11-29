all: webpage

webpage: gen webpage/webpage.html.part1 webpage/webpage.html.part2 webpage/webpage.js
	./gen | cat webpage/webpage.html.part1 - webpage/webpage.js webpage/webpage.html.part2 > index.html;

gen:
	cc -O3 -march=native gen.c -o gen

clean:
	rm -f index.html gen
