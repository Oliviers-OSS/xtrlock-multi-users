# xtrlock
xtrlock mod with some additional perks.

## Source
This is a fork of Ian Jackson's screen-locking utility. The source code is taken from Debian 10.4.

## Toppings
- Multiusers option and PAM authentication added to the original source code to allow a user to log into a generic user session

## Building and install
To enable authentication using PAM module, the preprocessoir symbol AUTH_USE_PAM *must be defined*
else the local shadow password will be used.
- To compile and install **usign imake** (X11 dev):
 1. Edit the Imakefile file to your likings.
 2. Type:
   $ xmkmf 
   $ make
   $ sudo make install
   $ sudo make install.man
     
- To compile and install **without imake** :
 1. Edit the Makefile.noimake file to your likings.
 2. Type:
   $ make -f Makefile.noimake
   $ sudo make -f Makefile.noimake install
   $ sudo make -f Makefile.noimake install.man

## Usage
The new command line option added is `-u` (for *Multi-users*). 
### Manual startup
To lock the mouse and keyboard and continue to see the display:
   ./xtrlock -u
The running account **must be a member of the shadow group** to be able to read the /etc/shadow file.

### Automtic startup in case of user inactivity 
You may use the xautolock program or equivalent, for example:
xautolock -time 5 -locker "/usr/bin/X11/xtrlock -u"
will start the xtrlock software in multi-users mode if there is no user activity during the last 5 minutes.

