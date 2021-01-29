# `system35.ctags`
This adds System 3.x language support to [Universal Ctags](https://ctags.io/) /
[Exuberant Ctags](http://ctags.sourceforge.net/). This means that if you are
using an editor that supports [Ctags](https://en.wikipedia.org/wiki/Ctags), you
can easily jump to the definition of functions and labels.

## Installation
If you are using universal-ctags, copy `system35.ctags` to `$(HOME)/.ctags.d/`.

If you are using exuberant-ctags, append the contents of `system35.ctags` to
`$(HOME)/.ctags`.

## Usage
In the source directory (where you have `*.ADV` files), run this command to
generate a `tags` file:
```
ctags -R
```
To generate a `TAGS` file for Emacs, add `-e` command-line option.

Check your editor's documentation for how to jump to a symbol definition.