---
layout: doc
title: types / function
---
# function

## General Concept

MudOS has a variable type named 'function'. Variables of this type may
be used to point to a wide variety of functions. You are probably already
familiar with the idea of passing a function to certain efuns. Take, for
example, the filter efun. It takes an array, and returns an array containing
the elements for which a certain function returns non-zero. Traditionally,
this was done by passing an object and a function name. Now, it can also
be done by passing an expression of type 'function' which merely contains
information about a function, which can be evaluated later.

Function pointers can be created and assigned to variables:

    function f = (: local_func :);

Passed to other routines or efuns, just like normal values:

    foo(f);
    map_array( ({ 1, 2 }), f);

Or evaluated at a later time:

    x = evaluate(f, "hi");

When the last line is run, the function that f points to is called, and "hi"
is passed to it. This will create the same effect as if you had done:

    x = local_func("hi");

The advantage of using a function pointer is that if you later want to
use a different function, you can just change the value of the variable.

Note that if evaluate() is passed a value that is not a function, it just
returns the value. So you can do something like:

    void set_short(mixed x) { short = x; }
    mixed query_short() { return evaluate(short); }

This way, simple objects can simply do: `set_short("Whatever")`, while objects
that want their shorts to change can do: `set_short( (: short_func :) )`;

## Available kinds of function pointers:

The simplest function pointers are the ones shown above. These simply
point to a local function in the same object, and are made using
`(: function_name :)`. Arguments can also be included; for example:

```c
string foo(string a, string b) {
    return "(" + a "," + b + ")";
}

void create() {
    function f = (: foo, "left" :);

    printf( "%s %s\n", evaluate(f), evaluate(f, "right") );

}
```
Will print:

    (left,0) (left,right)

The second kind is the efun pointer, which is just `(: efun_name :)`. This
is very similar to the local function pointer. For example, the objects()
efun takes a optional function, and returns all objects for which the
function is true, so:

    objects( (: clonep :) )

will return an array of all the objects in the game which are clones.
Arguments can also be used:

```c
void create() {
    int i;
    function f = (: write, "Hello, world!\n" :);

    for (i=0; i<3; i++) { evaluate(f); }

}
```

Will print:

    Hello, world!
    Hello, world!
    Hello, world!

Note that simul_efuns work exactly like efuns with respect to function
pointers.

The third type is the call_other function pointer, which is similar to the
type of function pointer MudOS used to support. The form is
`(: object, function :)`. If arguments are to be used, the should be added
to an array along with the function name. Here are some examples:

```c
void create()
{
    string *ret;
    function f = (: this_player(), "query" :);

    ret = map( ({ "name", "short", "long" }), f );
    write(implode(ret, "\n"));
}
```

This would print the results of `this_player()->query("name")`,
`this_player()->query("short")`, and `this_player()->query("long")`.
To make a function pointer that calls `query("short")` directly, use:

    f = (: this_player(), ({ "query", "short" }) :)

For reference, here are some other ways of doing the same thing:

```c
// a efun pointer using the call_other efun
f = (: call_other, this_player(), "query", "short" :);
// an expression functional
f = (: this_player()->query("short") :);
```

The fourth type is the expression function pointer. It is made using
`(: expression :)`. Within an expression function pointer, the arguments
to it can be refered to as $1, $2, \$3 ..., for example:

    evaluate( (: $1 + $2 :), 3, 4) // returns 7.

This can be very useful for using sort_array, for example:

    top_ten = sort_array( player_list,
    (: $2->query_level() - $1->query_level :) )[0..9];

The fifth type is an anonymous function:

```c
void create() {
    function f = function(int x) {
        int y;

        switch(x) {
            case 1: y = 3;break;
            case 2: y = 5;
        }
        return y - 2;
    };

    printf("%i %i %i\n", (*f)(1), (*f)(2), (*f)(3));
}
```

would print:

    1 3 -2

Note that `(*f)(...)` is the same as `evaluate(f, ...)` and is retained for
backwards compatibility. Anything that is legal in a normal function is
legal in an anonymous function.

## When are things evaluated?

The rule is that arguments included in the creation of efun, local function,
and simul_efun function pointers are evaluated when the function pointer is
made. For expression and functional function pointers, nothing is evaluated
until the function pointer is actually used:

```c
    // When it is _evaluated_, it will destruct
    // whoever "this_player()" was when it
    // was _made_
    (: destruct, this_player() :)

    // destructs whoever is "this_player()"
    // when the function is _evaluated_
    (: destruct(this_player()) :)
```

For this reason, it is illegal to use a local variable in an expression
pointer, since the local variable may no longer exist when the function
pointer is evaluated. However, there is a way around it:

    (: destruct( $(this_player) ) :) // Same as the first example above

`$(whatever)` means **evaluate whatever, and hold it's value, inserting it
when the function is evaluated**. It also can be used to make things more
efficient:

    map_array(listeners,
    (: tell_object($1, $(this_player()->query_name()) + " bows.\n") :) );

only does one call_other, instead of one for every message. The string
addition could also be done before hand:

    map_array(listeners,
    (: tell_object($1, $(this_player()->query_name() + " bows.\n")) :) );

Notice, in this case we could also do:

    map_array(listeners,
    (: tell_object, this_player()->query_name() + " bows.\n" :) );
