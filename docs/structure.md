```bash
RezzOS/
├── build.sh              # Main build script
├── start.sh              # Console mode launcher
├── start-gui.sh          # GUI mode launcher
├── init                  # Init script (first process)
│
├── usr/                  # User scripts
│   └── bin/
│       ├── pkg           # Package manager
│       ├── ghget         # File downloader
│       ├── rsv           # Runit service manager
│       ├── fetch         # System info
│
├── etc/                  # System configuration
│   ├── profile           # Shell settings
│   ├── tmux.conf         # Terminal multiplexer & scroll configuration
│   ├── passwd            # Users
│   ├── group             # Groups
│   ├── motd              # Login greeting
│   └── runit/            # Runit configuration
│       └── runsvdir/default/
│           ├── tty1/run  # Terminal service
│           └── network/run # Network service
│
├── root/                 # Root home directory
│   └── .bashrc           # Bash settings
│
├── docs/                 # Documentation
├── kernel.config         # Kernel configuration
├── .gitignore            # Git exclusions
├── LICENSE               # GPLv3 license
└── README.md             # Readme
