
// END OF AUTO-GENERATED CODE REGION

let searchbar = document.getElementById("search");
let lvdstbox = document.getElementById("lvdst");
let hide_tags = false;

// valid values: "plain", "exact", "regex", "levenshtein"
let wmatchmode = "plain";

// valid values: "words", "defs"
let searchmode = "words";

function levenshtein(x, y) {
	if (!x.length) return y.length;
	if (!y.length) return x.length;
	const arr = [];
	for (let i = 0; i <= y.length; i++) {
		arr[i] = [i];
		for (let j = 1; j <= x.length; j++) {
			arr[i][j] = i === 0 ? j : Math.min(
				arr[i - 1][j] + 1,
				arr[i][j - 1] + 1,
				arr[i - 1][j - 1] + (x[j - 1] === y[i - 1] ? 0 : 1)
			);
		}
	}
	return arr[y.length][x.length];
}

function match(x, y) {
	switch (wmatchmode) {
		case "plain": return y.includes(x);
		case "exact": return x === y;
		case "regex": return new RegExp(x).test(y);
		case "levenshtein": return levenshtein(x, y) <= lvdstbox.value;
	}
}

function shouldDisplayWord(input, word, defs, alltags) {
	const searchfortags = input.includes("#");

	// split at spaces except inside single quotes
	let terms = input.match(/(?:[^\s']+|'[^']*')+/g);
	terms.forEach((item, idx) => (terms[idx] = item.replaceAll("'", ""))); // remove single quotes

	let matchedword = false;
	let matcheddef = [];
	let matchedtags = [];
	let tagsonly = true;
	terms.forEach(function(item) {
		if (item[0] !== "#") {
			tagsonly = false;
			switch (searchmode) {
				case "words":
					if (match(item, word))
						matchedword = true;
					break;
				case "defs":
					defs.forEach(function(def, idx) {
						if (match(item, def))
							matcheddef.push(idx);
					});
			}
		} else {
			alltags.forEach(function(tags, idx) {
				if (tags.includes(item.slice(1)))
					matchedtags.push(idx);
			});
		}
	});
	if (tagsonly && matchedtags.length > 0)
		return true;
	let gottags = (!searchfortags || matchedtags.length > 0);
	let gotdef = (!searchfortags && matcheddef.length > 0) || matcheddef.some(e => matchedtags.includes(e));
	return gottags && (matchedword || gotdef);
}

function shouldDisplayDef(input, tags, def) {
	const searchfortags = input.includes("#");
	if (!searchfortags && searchmode !== "defs") // don't need to do anything
		return true;

	// split at spaces except inside single quotes
	let terms = input.match(/(?:[^\s']+|'[^']*')+/g);
	terms.forEach((item, idx) => (terms[idx] = item.replaceAll("'", ""))); // remove single quotes

	let matchedtags = false;
	let matcheddef = false;
	let tagsonly = true;
	terms.forEach(function(item) {
		if (item[0] === "#" && tags.includes(item.slice(1))) {
			matchedtags = true;
		} else if (item[0] !== "#") {
			tagsonly = false;
			if (searchmode === "defs" && match(item, def))
				matcheddef = true;
		}
	});
	return ((!searchfortags || matchedtags) && (tagsonly || (searchmode !== "defs" || matcheddef)));
}

function redraw(wordDisplayCallback, defDisplayCallback) {
	let mainarea = document.getElementById("main");
	mainarea.textContent = ""; // clear all children
	wordlist.forEach(function(item, idx) {
		if (wordDisplayCallback(item, deflist[idx], taglist[idx])) {
			mainarea.insertAdjacentHTML("beforeend", "<h3 class=\"inline\"><strong>" + item + "</strong></h3>");
			mainarea.insertAdjacentHTML("beforeend", "<ol>");
			desclist[idx].forEach(function(subitem, subidx) {
				let x = defDisplayCallback(taglist[idx][subidx], deflist[idx][subidx]) ? "" : " class=\"greyout\"";
				// this line is 225 cols long :face_holding_back_tears:
				mainarea.insertAdjacentHTML("beforeend", "<li" + x + "><i>" + subitem + "</i> " + deflist[idx][subidx] + (!hide_tags ? ("<br><p class=\"inline subtext\">tags: " + taglist[idx][subidx].join(" ") + "</p>") : "") + "</li>");
			});
			mainarea.insertAdjacentHTML("beforeend", "</ol><br>");
		}
	});
}

function refresh(input) {
	if (input === "")
		redraw(_ => true, _ => true);
	else
		redraw((word, defs, alltags) => shouldDisplayWord(input, word, defs, alltags),
			(tags, def) => shouldDisplayDef(input, tags, def));
}

function setwmmode(wmmode) {
	wmatchmode = wmmode;
	refresh(searchbar.value);
}

function setsearchmode(smode) {
	searchmode = smode;
	refresh(searchbar.value);
}

searchbar.addEventListener("input", (e) => refresh(e.target.value));
lvdstbox.addEventListener("input", _ => refresh(searchbar.value));
document.addEventListener("keypress", (e) => {
	if (e.code == "KeyH") { // h
		hide_tags = !hide_tags;
		refresh(searchbar.value);
	}
});
redraw(_ => true, _ => true);
