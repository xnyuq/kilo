# Kilo
## _Your own text editor_
Kilo is a minimal text editor for linux written in C with no dependencies or external libraries.\
Based on the original [kilo](https://github.com/antirez/kilo) and the tutorial [Build Your Own Text Editor](https://viewsourcecode.org/snaptoken/kilo/).
### Screenshot
![Screenshot](https://raw.githubusercontent.com/xnyuq/kilo/main/img/screenshot.png)
## Features
- Text editing
- Find

## Usage
```sh
# for editing an existed file
./kilo <filename>
# or with no argument to start editing a new file
./kilo
```
Keys
```
CTRL-S: Save
CTRL-Q: Quit
CTRL-F: Find string in file (ESC to exit search, arrows to navigate)
```
## Build

Kilo requires gcc to build.

```sh
make
```
## License
MIT License
