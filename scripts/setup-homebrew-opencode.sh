#!/bin/bash
# setup-homebrew-opencode.sh
# Auto-configure Homebrew and Homebrew-installed opencode for the target user.
# Usage: setup-homebrew-opencode.sh <username>

set -e

USERNAME="${1:-Gold}"
HOME_DIR="/home/${USERNAME}"
BREW_DIR="${HOME_DIR}/.homebrew"

setup_homebrew() {
    if [ -d "$BREW_DIR" ] && [ -x "$BREW_DIR/bin/brew" ]; then
        echo "--> Homebrew already installed at $BREW_DIR"
        return
    fi

    echo "--> Installing Homebrew (Linuxbrew) for user ${USERNAME}..."
    NONINTERACTIVE=1 runuser -l "${USERNAME}" -c \
        "NONINTERACTIVE=1 bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""

    if [ ! -f "${HOME_DIR}/.bashrc" ]; then
        touch "${HOME_DIR}/.bashrc"
    fi

    if ! grep -q 'homebrew' "${HOME_DIR}/.bashrc" 2>/dev/null; then
        cat <<'EOF' >> "${HOME_DIR}/.bashrc"

# Homebrew
eval "$(${HOME_DIR}/.homebrew/bin/brew shellenv)"
EOF
    fi

    echo "--> Homebrew installed successfully"
}

setup_opencode() {
    if command -v opencode &>/dev/null; then
        echo "--> opencode already installed"
        return
    fi

    echo "--> Installing opencode via Homebrew..."
    runuser -l "${USERNAME}" -c "brew install opencode"
    echo "--> opencode installed successfully"
}

setup_homebrew
setup_opencode