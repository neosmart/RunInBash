
Introduction
------------

`RunInBash` is a simple tool designed to make running Linux applications easier and faster under WSL. `RunInBash` simply runs whatever you type after it under a bash shell and pipes the output back to the parent command line. No need to worry about nested quotes, escaping arguments, or anything else.

## Installation

Just copy `RunInBash.exe` to your PATH. For best results, we recommend renaming it `$.exe`, allowing you to run any Linux command by simply prefixing it with `$`, e.g. `$ ifconfig` or `$ git status`.


Screenshots & Examples
------------

<img src="Screenshot.png" alt="betterpad screenshot" />

```
PS C:\git\RunInBash> git status
git : The term 'git' is not recognized as the name of a cmdlet, function, script file, or operable program. Check the
spelling of the name, or if a path was included, verify that the path is correct and try again.
At line:1 char:1
+ git status
+ ~~~
    + CategoryInfo          : ObjectNotFound: (git:String) [], CommandNotFoundException
    + FullyQualifiedErrorId : CommandNotFoundException

PS C:\git\RunInBash> $ git status
On branch master
Your branch is up-to-date with 'origin/master'.

nothing to commit, working directory clean
```

