# Programs

Sixty-one IRCIS programs, all of them bundled in the firmware. Copy them to
`/pircis/programs/` on an SD card, or paste one into the web editor, and they
show up under **PROG**.

On the device these are sorted into five folders -- Counting, Talking, Deciding,
Watching and Decoding -- which is how **PROG** opens. The headings below are
those folders, the tables give the name each program shows under there, and
where one is named in the prose the brackets say where to find it. The folders
are directories here, in the device's own storage and on the card alike, so
this listing, the repository and the device all agree.

Every one was checked against Arjun Nair's own interpreter before it shipped.
The ones that do not use `r`/`R` produce byte-identical output on the device.

**Where two programs would have been the same idea twice, they are not.** Nine
programs used to flip one coin and print one of two words; there are two now,
and they do it differently. Twelve answer machines used to walk the same ladder
of four tests; one still does, one uses a tree of coin flips instead, and one
draws twice and combines the results. Six multiplication tables used to be one
template with six constants, and are now six different routes to the same
answer. What is left that still shares a shape does so because the shape is
right.

[Dice Roll](Watching/Dice-Roll.txt) (Watching) is the one to try first. It
rolls, prints "You have rolled N", and puts N runners into a ring -- so the
number it printed is the number of coloured markers going round.

## Counting

The print loop with a different sum in it. No two of these reach their answer
the same way, which is the point of having more than one.

| Program | Size | Tag | What it does |
|---|---|---|---|
| [Countdown](Counting/Countdown.txt) | 5 x 15 |  | 10 down to 1, then GO |
| [Count to 20](Counting/Count-to-20.txt) | 5 x 20 |  | the skeleton with nothing added |
| [Odd Numbers](Counting/Odd-Numbers.txt) | 5 x 26 |  | doubles the counter and subtracts from a constant |
| [Even Numbers](Counting/Even-Numbers.txt) | 5 x 26 |  | the same trick, one off |
| [Threes](Counting/Threes.txt) | 5 x 26 |  | multiplication, plainly |
| [Seven Times](Counting/Seven-Times.txt) | 5 x 26 |  | multiplication, with the multiplier held in a variable |
| [Nine Times](Counting/Nine-Times.txt) | 5 x 35 |  | a running total: nine added each pass, never multiplied |
| [Eleven Times](Counting/Eleven-Times.txt) | 5 x 36 |  | ten times it plus one more of it -- two operations composed |
| [Doubles](Counting/Doubles.txt) | 5 x 29 |  | added to itself; no multiplication in it at all |
| [Quarters](Counting/Quarters.txt) | 5 x 35 |  | doubled by adding, then multiplied -- both operations together |
| [Sevens Plus One](Counting/Sevens-Plus-One.txt) | 5 x 25 |  | the only one whose counter climbs instead of falling |
| [Down By Three](Counting/Down-By-Three.txt) | 5 x 33 |  | 30 down to 3 |
| [Backwards](Counting/Backwards.txt) | 5 x 27 |  | 20 down to 1 |
| [Squares](Counting/Squares.txt) | 5 x 26 |  | the counter multiplied by itself |
| [Cubes](Counting/Cubes.txt) | 5 x 31 |  | three multiplications chained |
| [Oblong Numbers](Counting/Oblong-Numbers.txt) | 5 x 32 |  | n(n+1) |
| [Running Total](Counting/Running-Total.txt) | 5 x 25 |  | keeps a total across iterations |
| [Doubling](Counting/Doubling.txt) | 5 x 25 |  | 1 2 4 8 ... 512, doubling one variable |
| [Halving](Counting/Halving.txt) | 5 x 25 |  | 1024 back down to 2 |
| [Halving 1024](Counting/Halving-1024.txt) | 5 x 26 |  | dividend in a variable; 1024/3 lands on 341 |
| [Divide 720](Counting/Divide-720.txt) | 5 x 28 |  | dividend written into the sum, until 7 spoils it |
| [Leftovers](Counting/Leftovers.txt) | 5 x 28 |  | modulo -- the only one whose output does not climb |
| [Fibonacci](Counting/Fibonacci.txt) | 5 x 27 |  | two variables swapped each pass |
| [Fahrenheit](Counting/Fahrenheit.txt) | 5 x 34 |  | Celsius to Fahrenheit, and the one you might use |
| [Pi](Counting/Pi.txt) | 6 x 77 |  | ten correct digits from Machin's formula, with 32-bit integers and no arrays |

## Talking

A string laid backwards in the grid and walked out one character at a time.
Most of these share one shape, and it is the right one: printing fixed text
has a single natural implementation, and what differs is what they say. Three
of them take the same string for a walk instead -- a snake, a wave and a
spiral -- to show that the path the runner reads it along can be any shape at
all, as long as the characters arrive in the right order.
[Warning](Talking/Warning.txt) (Talking) is the other exception, and
deliberately -- see below.

| Program | Size | Tag | What it does |
|---|---|---|---|
| [Hello World](Talking/Hello-World.txt) | 2 x 18 |  | the loop in its barest form; read this one first |
| [SOS](Talking/SOS.txt) | 2 x 17 |  | three dots, three dashes, three dots |
| [Greeting](Talking/Greeting.txt) | 6 x 31 |  | the string read along a square wave: across, down, across, up |
| [Quick Brown Fox](Talking/Quick-Brown-Fox.txt) | 2 x 50 |  | the pangram, for checking every glyph renders |
| [Liquor Jugs](Talking/Liquor-Jugs.txt) | 2 x 46 |  | a second pangram, shorter and rarer |
| [Two Dimensions](Talking/Two-Dimensions.txt) | 4 x 17 |  | the string read boustrophedon: one row east, the next west |
| [Advice](Talking/Advice.txt) | 7 x 14 |  | the string read along a spiral, closing on the print loop at its centre |
| [Excuse](Talking/Excuse.txt) | 2 x 35 |  | an excuse in the language's own terms |
| [Warning](Talking/Warning.txt) | 4 x 72 |  | an honest self-assessment, and the only one here whose words are not in the grid |
| [Morse A to M](Talking/Morse-A-to-M.txt) | 2 x 71 |  | the first half of the Morse alphabet, as a card |
| [Morse N to Z](Talking/Morse-N-to-Z.txt) | 2 x 73 |  | the second half |

## Deciding

Anything that picks, and there are four different ways of picking here: no
branch at all, a branch whose arms meet again, a tree of coin flips, and a
ladder of tests. One program for each way of doing it.

| Program | Size | Tag | What it does |
|---|---|---|---|
| [Coin Flip](Deciding/Coin-Flip.txt) | 4 x 36 |  | no branch at all: one random bit picks between two constants that % prints as HEADS and TAILS |
| [True Or False](Deciding/True-Or-False.txt) | 6 x 41 |  | two arms that meet again -- each prints a counted number of characters, so both reach one tail |
| [Magic Eight Ball](Deciding/Magic-Eight-Ball.txt) | 11 x 31 |  | two coin flips make a tree with four leaves |
| [Dog Name](Deciding/Dog-Name.txt) | 15 x 34 |  | two draws, and not a readable word in it: sixteen names out of eight hidden strings |
| [Fortune](Deciding/Fortune.txt) | 15 x 72 |  | a ladder of four tests -- fortune-cookie lines |
| [Dumb Clock](Deciding/Dumb-Clock.txt) | 10 x 25 |  | invents a time and reads it out in words; the device has no clock, which is the joke |

## Watching

The runners are the point. The first eight print nothing at all, and carry a
view tag asking for nothing under the grid and a slow run, because at SLOW a
runner leaves a fading trail and the trail is what you are there for. The rest
do print something -- a motto, a winner, a die roll, six lottery numbers --
but what happens on the grid while they get there is the show.

| Program | Size | Tag | What it does |
|---|---|---|---|
| [Spiral](Watching/Spiral.txt) | 10 x 31 | `~ns` | winds inward over every cell |
| [Bounce](Watching/Bounce.txt) | 7 x 28 | `~ns` | a ball zigzags to the right, rebounding off the top and bottom |
| [Snake](Watching/Snake.txt) | 10 x 30 | `~ns` | the boustrophedon weave |
| [Serpent](Watching/Serpent.txt) | 10 x 30 | `~ns` | the weave turned on its side: down one column, up the one after next |
| [Staircase](Watching/Staircase.txt) | 8 x 28 | `~ns` | steps diagonally down |
| [Circuit](Watching/Circuit.txt) | 8 x 28 | `~ns` | a closed loop, lapped forever |
| [Comb](Watching/Comb.txt) | 6 x 29 | `~ns` | along the spine, down each tooth and back up the next |
| [Four Ways](Watching/Four-Ways.txt) | 7 x 28 | `~ns3,13` | the runner starts on a split with an arrow on every side, and four go out at once |
| [Motto](Watching/Motto.txt) | 11 x 44 | `~st8,N` | each letter of IRCIS is walked by its own runner, and each runner prints its own word of the motto; the words are numbers, and the grid holds no letters at all |
| [Race](Watching/Race.txt) | 10 x 32 | `~ns` | four runners with random handicaps, on one screen |
| [Racetrack](Watching/Racetrack.txt) | 15 x 80 | `~nm` | three runners, five random pit stops each, identical lanes and cancelling handicaps |
| [Dice Roll](Watching/Dice-Roll.txt) | 9 x 28 |  | rolls, prints the number, then puts that many runners into a ring |
| [Dumb Pi](Watching/Dumb-Pi.txt) | 8 x 77 |  | eight real digits, then it makes the rest up, forever |
| [Nothing To See](Watching/Nothing-To-See.txt) | 10 x 45 |  | a paragraph of plain English insisting it does nothing. Column 29, read downward, is `'ircis.%!` |
| [Lottery](Watching/Lottery.txt) | 5 x 21 |  | six numbers from 1 to 49, out of one bounded random |

## Decoding

Programs about how things are written down: words stored as numbers, the sum
that turns a number back into a word, a count written out in binary, and the
one program here that reads what you type.

| Program | Size | Tag | What it does |
|---|---|---|---|
| [Insult Machine](Decoding/Insult-Machine.txt) | 4 x 73 |  | a grid of nothing but numbers, each decoding through base64 into a word |
| [Base 64](Decoding/Base-64.txt) | 14 x 75 |  | works out `HEADS` from its digit values by hand, then writes `'HEADS.` and gets the same number |
| [Morse Decoder](Decoding/Morse-Decoder.txt) | 28 x 96 |  | type dits and dahs on the top row, get the word back |
| [Binary](Decoding/Binary.txt) | 8 x 22 |  | binary, least significant bit first (from upstream IRCIS) |

## The morse decoder

[Morse Decoder](Decoding/Morse-Decoder.txt) (Decoding) is the binary tree
from
[101computing](https://www.101computing.net/morse-code-using-a-binary-tree/), in
the array form: start at index 1, and for each symbol do `index = index*2 +
bit`. Every code lands on its own number, and those numbers run 2 to 29, so the
letters sit in the classic flattened order -- `ETIANMSURWDKGOHVF+L+PJBXCYZQ`.
The answer is the index-th one.

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
  the integer as base64. [Coin Flip](Deciding/Coin-Flip.txt) (Deciding) is one
  line and has no `?` in it.
- **A branch that meets again.** `?` tests the top of the stack without
  popping, so each arm has a leftover to drop. If an arm prints a *counted*
  number of characters rather than looping until the stack runs dry, it comes
  out alive and the arms can share a tail. [True Or
  False](Deciding/True-Or-False.txt) (Deciding) does this.
- **A tree.** Two flips make four leaves. The leaves come out on four different
  columns, all to the left of where the printers start, so a leaf can fall past
  the printers above it. [Magic Eight Ball](Deciding/Magic-Eight-Ball.txt)
  (Deciding) is the smallest example.
- **A ladder.** One bounded random into a variable, then one test per outcome,
  each rung stepping it down. The widest of the four, and the one that scales
  past four outcomes. [Fortune](Deciding/Fortune.txt) (Deciding) walks one.

[Dog Name](Deciding/Dog-Name.txt) (Deciding) does it twice over: four firsts
and four seconds give sixteen answers out of eight strings, which no single
pick of four can manage.

## A fourth: put the text in the path

[Motto](Watching/Motto.txt) (Watching) has not a letter in it. The words are
numbers: `%` prints an integer as base64, and in base64 `Run` is 72615,
`Chars` is 42314476, `See` is 75678 and `I` is 8. The runner starts at the
foot of the first column, heading north, and reads `'72615.` on its way up;
along the top row a `*` at each letter splits one runner downward to walk
the shape of that letter, and every runner takes a copy of the stack with it.
So the R's runner finds 72615 waiting and prints it; the C's and the S's get
their numbers pushed along the top row just before they split off; and the
two I's push their own 8 on the way down their strokes. The one space in the
program is at the top left, `" "`, and it is copied up with `@1.` and `@0.`
wherever a word needs one and printed with `#`. Each runner's printing cells
sit on its own stroke, on cells no other runner walks, so the motto comes
out in order across runners that are all going at once: `I` at step 17,
` Run` at 21 and 22, ` Chars` at 36 and 37, ` I ` from 40 to 45, `See` at
52. The strokes are otherwise blank cells -- a runner crossing a blank keeps
going -- so only the turns need a character, and the letterform is the
trajectory rather than the picture. The `~t` tag keeps every cell a runner
has stood on tinted, so the letters stay drawn.

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

- [Insult Machine](Decoding/Insult-Machine.txt) (Decoding) is a flat list
  of constants. Every word in the message is one number, printed in order, and
  the grid holds nothing but digits. - [Dog Name](Deciding/Dog-Name.txt)
  (Deciding) does the same, but which constants get printed is decided by two
  coin flips, so it hides sixteen answers rather than one. You can see it
  choosing; you cannot see what it is choosing between. -
  [Warning](Talking/Warning.txt) (Talking) does not even write the constants
  down. Each is stored as a quotient and a remainder and multiplied back out at
  run time, so the numbers you can read are not the numbers being printed. It is
  the longest and busiest program in the set, and it announces that it does
  nothing.

One catch worth knowing if you try it: `%` strips leading zeros, and in base64
zero is the letter `A`. A chunk beginning with `A` comes out a character short,
which is why the dog is no longer called Admiral.

## How the counting ones are built

Most are the same shape: a counter in a global, a body that runs east along one
row and back west along the next, and a `?` that drops into an exit lane when
the counter reaches zero. Reading a westward row means reading it right to left,
which is why those rows look like `^?.N&.-'.N@.1'<`. [Sevens Plus
One](Counting/Sevens-Plus-One.txt) (Counting) is the exception -- its counter
climbs and the test is against the limit.

Two things worth knowing if you write your own:

- Arithmetic takes the **top** of the stack as the left operand. Push 10 then 3
  and `'-.` gives -7, not 7. Division is the same way round.
- A single quote starts a number and a blank ends it: `'42.` pushes 42. An
  all-digit literal is decimal; anything else is base64, so `'fU.` is not 42.

## Credit

[Binary](Decoding/Binary.txt) (Decoding) is from [Arjun Nair's
IRCIS](https://github.com/batman-nair/IRCIS) unchanged.
[Racetrack](Watching/Racetrack.txt) (Watching) began as his example too,
but is a rebuild rather than his file -- his is 19 x 70, this one is 15 x 80 to
fill the screen in WIDE, and the lanes were reworked so the race is actually
fair. The rest are new.
