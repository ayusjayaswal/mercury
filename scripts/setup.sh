#!/bin/bash

# Mercury Text Storage Server - System Service Setup Script
# Run with sudo: sudo ./setup-service.sh

set -e

echo "Setting up Mercury Text Storage Server as a system service..."

# Check if running as root
if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root (use sudo)" 
   exit 1
fi

# Create mercury user and group
echo "Creating mercury user and group..."
if ! id "mercury" &>/dev/null; then
    useradd --system --shell /bin/false --home /var/lib/mercury --create-home mercury
    echo "Created mercury user"
else
    echo "mercury user already exists"
fi

# Create directories
echo "Creating directories..."
mkdir -p /var/lib/mercury
mkdir -p /var/log/mercury
chown mercury:mercury /var/lib/mercury
chown mercury:mercury /var/log/mercury

# Check if mercury binary exists
if [[ ! -f /usr/local/bin/mercury ]]; then
    echo "Error: mercury binary not found at /usr/local/bin/mercury"
    echo "Please run 'make install' first to install the binary"
    exit 1
fi

# Set proper permissions on the binary
chmod 755 /usr/local/bin/mercury

# Copy systemd service file
echo "Installing systemd service file..."
if [[ -f systemd/mercury.service ]]; then
    cp systemd/mercury.service /etc/systemd/system/
    echo "Copied systemd/mercury.service to /etc/systemd/system/"
else
    echo "Failed Creating systemd service file..."
fi

# Reload systemd and enable service
echo "Reloading systemd and enabling service..."
systemctl daemon-reload
systemctl enable mercury.service

echo ""
echo "==================================================="
echo "Mercury service setup complete!"
echo "==================================================="
echo ""
echo "Service commands:"
echo "  sudo systemctl start mercury    - Start the service"
echo "  sudo systemctl stop mercury     - Stop the service"
echo "  sudo systemctl restart mercury  - Restart the service"
echo "  sudo systemctl status mercury   - Check service status"
echo "  sudo systemctl enable mercury   - Enable auto-start (already done)"
echo "  sudo systemctl disable mercury  - Disable auto-start"
echo ""
echo "Logs:"
echo "  sudo journalctl -u mercury -f   - Follow live logs"
echo "  sudo journalctl -u mercury      - View all logs"
echo ""
echo "The service will start automatically on boot."
echo "To start it now, run: sudo systemctl start mercury"
echo ""
