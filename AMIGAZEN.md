# amigazen project

amigazen project is reviving old abandoned Open Source projects for Amiga making sure they can build and run on the latest Amiga platforms with a standard development toolchain, making them accessible again for users to enjoy and developers to enhance.

## [amigazen project](http://www.amigazen.com)

*A web, suddenly*

*Forty years meditation*

*Minds awaken, free*

**amigazen project** uses modern software development tools and methods to update and rerelease classic Amiga open source software. Releases include a new AWeb, this new Amiga Python 2, and the ToolKit project - a universal SDK for Amiga.

Key to the amigazen project approach is ensuring every project can be built with the same common set of development tools and configurations, so the ToolKit project was created to provide a standard configuration for Amiga development. All *amigazen project* releases will be guaranteed to build against the ToolKit standard so that anyone can download and begin contributing straightaway without having to tailor the toolchain for their own setup.

The amigazen project philosophy is based on openness:

*Open* to anyone and everyone	- *Open* source and free for all	- *Open* your mind and create!

PRs for all of amigazen projects are gratefully received at [GitHub](https://github.com/amigazen/). While the focus now is on classic 68k software, it is intended that all amigazen project releases can be ported to other Amiga-like systems including AROS and MorphOS where feasible.

## amigazen releases

The following projects are currently Work in Progress and available at https://github.com/amigazen/

- **Amiga Python 2** - Irmen de Jong's Amiga Python 2 port updated to the last release of Python 2.7.18
- **AWeb 3** - Yvon Rozijn's AWeb APL edition updated to build with Roadshow and Reaction
- **unsui** including _UniLib3_ and _shami_ - a POSIX runtime environment including C standard library, shell and command line tools
- _UniLib3_ An updated, POSIX-compatible C library based on David Gay's unix.lib and including new implementations of pthread, librt, psockets, iconv, regex and more
- **ToolKit** - SDK configurations and tools to use alongside the Amiga NDK including:
- _Codex_ - a new Amiga coding standards C linting tool
- _make-amiga_ - the last Amiga compatible version of GNU make
- _ctags-amiga_ and _etags-amiga_ - Exactly as it says ctags and etags, but for Amiga
- _rman-amiga_ - a port of the PolyglotMan man page generator
- _texinfo-amiga_ - the makeinfo command for converting info format documents into man pages, AmigaGuide docs, printed manuals and more
- _FileTypes_ - Python and MarkDown FileType syntax highlighting plugins for TextEdit and CodeCraft
- _z.library_ - zlib as a shared library - updated to the latest zlib
- _asyncio.library_ - Martin Taillefer's asyncio shared library - bug fixed
- _gtlayout.library_ - Olaf Barthel's GadTools layout shared library - updated with SFD file and new style header files
- _reaction.lib_sasc_ - a link library for Reaction and SAS/C to auto open class libraries
- _OpenTriton_ - Stefan Zeiger's triton.library GUI framework 
- _Insight_ - after meditation comes... Insight, a guru error LastAlert tool
- _CShell_ - Matt Dillon's CShell an Amiga native csh-like shell
- _FileTypes_ - MarkDown and Python FileType plugins for TextEdit and CodeCraft
- _iff2png_ - Convert any IFF format picture (not only ILBM, ALSO DEEP, RGB8 etc.) to PNG format

## About ToolKit

**ToolKit** exists to solve the problem that most Amiga software was written in the 1980s and 90s, by individuals working alone, each with their own preferred setup for where their dev tools are run from, where their include files, static libs and other toolchain artifacts could be found, which versions they used and which custom modifications they made. Open source collaboration did not exist as we know it in 2025. 

**ToolKit** from amigazen project is a work in progress to make a standardised installation of not just the Native Developer Kit, but the compilers, build tools and third party components needed to be able to consistently build projects in collaboration with others, without each contributor having to change build files to work with their particular toolchain configuration. 

All *amigazen project* releases will release in a ready to build configuration according to the ToolKit standard.

Each component of **ToolKit** is open source and will have it's own github repo, while ToolKit itself will eventually be released as an easy to install package containing the redistributable components, as well as scripts to easily install the parts that are not freely redistributable from archive.

**ToolKit** is a simple and standard way to configure your development toolchain to ensure that developers collaborating on projects do not need to keep changing a project's configuration every time they push or pull new work, and public releases will build straight out of the box.

## Frequently Asked Questions

### What's involved in releasing an amigazen project?

Reincarnating old Amiga open source projects typically involves steps like:

- Ensuring at least one of these Amiga C compilers is supported by the project's build system: SAS/C, VBCC, GCC or DICE, which may in involve rewriting old Lattice or Aztec makefiles
- Rewriting makefiles
- Finding other build toolchain dependencies and incorporating those into _ToolKit_ as well, such as rman and ctags
- Reorganising project directory contents
- Writing build instructions
- Importing into GitHub the original version, and then maintaining a commit history of changes made
- Checking open source license compliance
- Modifying code to work with the latest Amiga NDK include files
- Updating version numbers and build dates
- Rewriting code to replace outdated dependencies
- Refactoring code to compile correctly with the latest version of the relevant compiler
- Adding support for building with other Amiga compilers

All projects will be updated over time to use features of the latest version of the operating system such as memory pools, and add new features requested by users.

### What kind of Amiga is needed to run these projects?

All amigazen projects make no compromises to support older versions of Amiga and no apologies for doing so. The purpose is to move forward not look to the past. This is not a retro project. 

Every amigazen project assumes a healthy amount of RAM, harddisk space and CPU speed, and that the user is probably using some kind of enhanced Amiga whether an emulated Amiga or some modern Amiga-like hardware. All projects are built against the latest versions of the C compilers and NDK.

### Are all amigazen projects Open Source?

Yes amigazen project is committed to free and open source software for the Amiga and all releases include full source code and open source licensing. As much as possible development will be conducted in public offering full transparency and opportunities for collaboration.

### Can I submit a Pull Request for some improvements to an amigazen project

Yes please!

### What is the _zen_ of Amiga?

For thoughts from amigazen project on this question please visit http://www.amigazen.com/zen/

What part of Amiga hobby is your _zen_?

### What is the meaning of amigazen?

_ami gazen_ is a poetic way to say 'a web, suddenly' in Japanese and reflects the name of the first amigazen project, a revitalisation of AWeb. 

## Contact 

- At GitHub https://github.com/amigazen/
- on the web at http://www.amigazen.com/ (Amiga browser compatible)
- or email zen@amigazen.com

## Acknowledgements

*Amiga* is a trademark of **Amiga Inc**. 
