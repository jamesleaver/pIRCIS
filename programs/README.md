# Programs

Sixty-eight IRCIS programs, all of them bundled in the firmware. Copy them to
`/pircis/programs/` on an SD card, or paste one into the web editor, and they
show up under **PROG**.

On the device these are sorted into five folders -- Counting, Talking,
Deciding, Watching and Showing-off -- which is how **PROG** opens. The folders
are real directories in the device's own storage and on the card, so this
listing and the device agree. In this repository they are kept flat, since
`tools/gen_examples.py` is what assigns the folders.

Every one was checked against Arjun Nair's own interpreter before it shipped.
The ones that do not use `r`/`R` produce byte-identical output on the device.

**Where two programs would have been the same idea twice, they are not.** Nine
programs used to flip one coin and print one of two words; there are two now,
and they do it differently. Twelve answer machines used to walk the same ladder
of four tests; six still do, four use a tree of coin flips instead, and two draw
twice and combine the results. Six multiplication tables used to be one template
with six constants, and are now six different routes to the same answer. What is
left that still shares a shape does so because the shape is right.

`dice` is the one to try first. It rolls, prints "You have rolled N", and puts
N runners into a ring -- so the number it printed is the number of coloured
markers going round.

## Counting

The print loop with a different sum in it. No two of these reach their answer the same way, which is the point of having more than one.

| Program | Size | Tag | What it does |
|---|---|---|---|
| `hello` | 2 x 18 |  | the loop in its barest form; read this one first |
| `countdown` | 5 x 15 |  | 10 down to 1, then GO |
| `count` | 5 x 20 |  | the skeleton with nothing added |
| `odds` | 5 x 26 |  | doubles the counter and subtracts from a constant |
| `evens` | 5 x 26 |  | the same trick, one off |
| `threes` | 5 x 26 |  | multiplication, plainly |
| `times7` | 5 x 26 |  | multiplication, with the multiplier held in a variable |
| `nines` | 5 x 35 |  | a running total: nine added each pass, never multiplied |
| `elevens` | 5 x 36 |  | ten times it plus one more of it -- two operations composed |
| `doubles` | 5 x 29 |  | added to itself; no multiplication in it at all |
| `quarters` | 5 x 35 |  | doubled by adding, then multiplied -- both operations together |
| `gaps` | 5 x 25 |  | the only one whose counter climbs instead of falling |
| `minusthree` | 5 x 33 |  | 30 down to 3 |
| `backwards` | 5 x 27 |  | 20 down to 1 |
| `squares` | 5 x 26 |  | the counter multiplied by itself |
| `cubes` | 5 x 31 |  | three multiplications chained |
| `oblong` | 5 x 32 |  | n(n+1) |
| `triangle` | 5 x 25 |  | keeps a total across iterations |
| `powers2` | 5 x 25 |  | 1 2 4 8 ... 512, doubling one variable |
| `halving` | 5 x 25 |  | 1024 back down to 2 |
| `halved` | 5 x 26 |  | dividend in a variable; 1024/3 lands on 341 |
| `divisors` | 5 x 28 |  | dividend written into the sum, until 7 spoils it |
| `leftover` | 5 x 28 |  | modulo -- the only one whose output does not climb |
| `fib` | 5 x 27 |  | two variables swapped each pass |
| `fahrenheit` | 5 x 34 |  | Celsius to Fahrenheit, and the one you might use |
| `binary` | 8 x 22 |  | binary, least significant bit first (from upstream IRCIS) |

## Talking

A string laid backwards in the grid and walked out one character at a time. Ten of these share one shape, and it is the right one: printing fixed text has a single natural implementation, and what differs is what they say. `warning` and `motto` are the exceptions, and deliberately -- see below.

| Program | Size | Tag | What it does |
|---|---|---|---|
| `sos` | 2 x 17 |  | three dots, three dashes, three dots |
| `greeting` | 2 x 27 |  | a hello that names where it came from |
| `lorem` | 2 x 50 |  | the pangram, for checking every glyph renders |
| `pangram` | 2 x 46 |  | a second pangram, shorter and rarer |
| `esolang` | 2 x 41 |  | a one-line manifesto |
| `advice` | 2 x 31 |  | turn it off and on again |
| `excuse` | 2 x 35 |  | an excuse in the language's own terms |
| `motto` | 13 x 46 | `~nst` | five runners draw IRCIS by walking it, then it prints what that stands for |
| `warning` | 4 x 72 |  | an honest self-assessment, and the only one here whose words are not in the grid |
| `morse1` | 2 x 71 |  | the first half of the Morse alphabet, as a card |
| `morse2` | 2 x 73 |  | the second half |

## Deciding

Anything that picks, and there are four different ways of picking here: no branch at all, a branch whose arms meet again, a tree of coin flips, and a ladder of tests. Where two programs would have been the same idea twice, there is now one.

| Program | Size | Tag | What it does |
|---|---|---|---|
| `coin` | 4 x 36 |  | no branch at all: one random bit picks between two constants that % prints as HEADS and TAILS |
| `truefalse` | 6 x 41 |  | two arms that meet again -- each prints a counted number of characters, so both reach one tail |
| `eightball` | 11 x 31 |  | two coin flips make a tree with four leaves |
| `weather` | 11 x 34 |  | a tree as well, with no regard for the season |
| `mood` | 11 x 33 |  | a tree; tells you how you are feeling |
| `verdict` | 11 x 26 |  | a tree; guilty or not |
| `dogname` | 15 x 34 |  | two draws, and not a readable word in it: sixteen names out of eight hidden strings |
| `band` | 15 x 32 |  | two draws again, and it occasionally names itself |
| `fortune` | 15 x 72 |  | a ladder of four tests -- fortune-cookie lines |
| `excuses` | 15 x 76 |  | a ladder; a different reason each run |
| `horoscope` | 15 x 69 |  | a ladder; vague enough to always be true |
| `compliment` | 15 x 67 |  | a ladder; says something nice |
| `lunch` | 15 x 65 |  | a ladder; decides lunch so you do not have to |
| `advice2` | 15 x 72 |  | a ladder, with a wider bench than `advice` |
| `lotto` | 5 x 21 |  | six numbers from 1 to 49, out of one bounded random |
| `clock` | 10 x 25 |  | invents a time and reads it out in words; the device has no clock, which is the joke |

## Watching

No output -- the runners are the point. Each carries a view tag asking for nothing under the grid and a slow run, because at SLOW a runner leaves a fading trail and the trail is the whole point.

| Program | Size | Tag | What it does |
|---|---|---|---|
| `spiral` | 10 x 31 | `~ns` | winds inward over every cell |
| `bounce` | 7 x 28 | `~ns` | ricochets off the walls |
| `snake` | 10 x 30 | `~ns` | the boustrophedon weave |
| `serpent` | 9 x 28 | `~ns` | a longer weave with a doubled-back tail |
| `staircase` | 8 x 28 | `~ns` | steps diagonally down |
| `circuit` | 8 x 28 | `~ns` | a closed loop, lapped forever |
| `comb` | 6 x 28 | `~ns` | down each tooth and back up |
| `fourways` | 5 x 28 | `~ns` | one split sends four runners out at once |

## Showing off

Programs that get somewhere the language was not built to go.

| Program | Size | Tag | What it does |
|---|---|---|---|
| `insult` | 4 x 73 |  | a grid of nothing but numbers, each decoding through base64 into a word |
| `pi` | 6 x 77 |  | ten correct digits from Machin's formula, with 32-bit integers and no arrays |
| `racetrack` | 15 x 80 | `~nm` | three runners, five random pit stops each, identical lanes and cancelling handicaps |
| `dumbpi` | 8 x 77 |  | eight real digits, then it makes the rest up, forever |
| `dice` | 9 x 28 |  | rolls, prints the number, then puts that many runners into a ring |
| `race` | 10 x 32 | `~ns` | four runners with random handicaps, on one screen |
| `morsedecode` | 28 x 96 |  | type dits and dahs on the top row, get the word back |

## The morse decoder

`morsedecode` is the binary tree from
[101computing](https://www.101computing.net/morse-code-using-a-binary-tree/),
in the array form: start at index 1, and for each symbol do
`index = index*2 + bit`. Every code lands on its own number, and those numbers
run 2 to 29, so the letters sit in the classic flattened order --
`ETIANMSURWDKGOHVF+L+PJBXCYZQ`. The answer is the index-th one.

The whole of row 0 is the input, and the three rows under it say what to type.
The runner never reads them: it comes down the right-hand end of row 0 and falls
straight through a single `v` on each, missing every other cell. That is why the
instructions sit at the top where they are on screen, instead of under the
program where you would have to scroll to find them.

One loop, and nothing branches inside it. Two tricks make that possible.

The digit step is arithmetic rather than a test. With `bit = d(d-1)/2` and
`mult = 1+d-bit`, the step `index = index*mult + bit` does the right thing for a
dit (1), a dah (2) *and* a zero, which leaves the index alone -- so every code
runs the same four steps and the short ones pad themselves.

The table lookup is arithmetic too. `1/(1+(q-j)^2)` is 1 when `q` equals `j` and
0 otherwise, because the division is integer, so picking the right constant out
of six is a sum of six terms and not a ladder of six tests.

Codes come off the stack last-letter-first, which is why the tree is walked from
the last symbol back -- the letters sit in the order
`ETINAMSDRGUKWOHBLZFCP+VX+Q+YJ` rather than the textbook one. The answers are
accumulated as `answer = answer + letter * 64^n`, so that same reversal puts the
first letter in the highest digit and `%` prints the word the right way round.
Five letters is the ceiling: an int32 holds exactly five base64 characters.

A drawn tree would look better and does not fit: sixteen leaves at depth four
need more than 96 columns once every node carries its own test.

## Four ways to pick one of several

Worth knowing if you write your own, because the grid pushes you towards one of
them:

- **No branch.** If every outcome is at most five characters, a random draw can
  be turned into the answer with arithmetic and printed with `%`, which reads
  the integer as base64. `coin` is one line and has no `?` in it.
- **A branch that meets again.** `?` tests the top of the stack without popping,
  so each arm has a leftover to drop. If an arm prints a *counted* number of
  characters rather than looping until the stack runs dry, it comes out alive
  and the arms can share a tail. `truefalse` does this.
- **A tree.** Two flips make four leaves. The leaves come out on four different
  columns, all to the left of where the printers start, so a leaf can fall past
  the printers above it. `eightball` is the smallest example.
- **A ladder.** One bounded random into a variable, then one test per outcome,
  each rung stepping it down. The widest of the four, and the one that scales
  past four outcomes. `fortune` and five others.

`dogname` and `band` do it twice over: four firsts and four seconds give sixteen
answers out of eight strings, which no single pick of four can manage.

## A fourth: put the text in the path

`motto` has no text in it at all. Row 0 is a rule of `>` that the first runner
walks east along; at each letter a `*` splits one runner downward, and that
runner walks the shape of a letter. The strokes are blank cells -- a runner
crossing a blank keeps going -- so only the turns need a character, and the
letterform is the trajectory rather than the picture. The `~t` tag keeps every
cell a runner has stood on tinted, so five letters get drawn at once and stay
drawn.

Two things constrain the shapes, and both are worth knowing before trying your
own. `*` splits only towards a neighbour holding that direction's own arrow, so
two splits cannot sit side by side -- the second is not a `>`, and the rule
stops there. And no stroke may cross one of its own turns: a cell holds one
arrow, and a second pass takes the turn again and loops for ever. That is why R
needs two runners, one down for the stem and one up off the foot rule for the
bowl and leg -- a single path cannot get back past its own stem.

## Three ways to hide what a program says

Three of these give nothing away until you run them. Each hides its text a
different way, and all three lean on `%`, which prints an integer as base64 --
so a word can be carried as a number instead of as itself.

- **`insult`** is a flat list of constants. Every word in the message is one
  number, printed in order, and the grid holds nothing but digits.
- **`dogname`** does the same, but which constants get printed is decided by
  two coin flips, so it hides sixteen answers rather than one. You can see it
  choosing; you cannot see what it is choosing between.
- **`warning`** does not even write the constants down. Each is stored as a
  quotient and a remainder and multiplied back out at run time, so the numbers
  you can read are not the numbers being printed. It is the longest and busiest
  program in the set, and it announces that it does nothing.

One catch worth knowing if you try it: `%` strips leading zeros, and in base64
zero is the letter `A`. A chunk beginning with `A` comes out a character short,
which is why the dog is no longer called Admiral.

## How the counting ones are built

Most are the same shape: a counter in a global, a body that runs east along one
row and back west along the next, and a `?` that drops into an exit lane when
the counter reaches zero. Reading a westward row means reading it right to left,
which is why those rows look like `^?.N&.-'.N@.1'<`. `gaps` is the exception --
its counter climbs and the test is against the limit.

Two things worth knowing if you write your own:

- Arithmetic takes the **top** of the stack as the left operand. Push 10 then 3
  and `'-.` gives -7, not 7. Division is the same way round.
- A single quote starts a number and a blank ends it: `'42.` pushes 42. An
  all-digit literal is decimal; anything else is base64, so `'fU.` is not 42.

## Credit

`binary` is from [Arjun Nair's IRCIS](https://github.com/batman-nair/IRCIS)
unchanged. `racetrack` began as his example too, but is a rebuild rather than
his file -- his is 19 x 70, this one is 15 x 80 to fill the screen in WIDE, and
the lanes were reworked so the race is actually fair. The rest are new.
