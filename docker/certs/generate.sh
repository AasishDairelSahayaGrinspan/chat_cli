#!/bin/bash
# Generate self-signed certificates for development

set -e

CERT_DIR="$(dirname "$0")"
cd "$CERT_DIR"

# Generate DH parameters (this takes a while)
if [ ! -f dh2048.pem ]; then
    echo "Generating DH parameters..."
    openssl dhparam -out dh2048.pem 2048
fi

# Generate CA key and certificate
if [ ! -f ca.key ]; then
    echo "Generating CA..."
    openssl genrsa -out ca.key 4096
    openssl req -new -x509 -days 3650 -key ca.key -out ca.crt \
        -subj "/C=US/ST=Dev/L=Dev/O=ChatDev/CN=Chat CA"
fi

# Generate server key and certificate
if [ ! -f server.key ]; then
    echo "Generating server certificate..."
    openssl genrsa -out server.key 2048

    # Create CSR
    openssl req -new -key server.key -out server.csr \
        -subj "/C=US/ST=Dev/L=Dev/O=ChatDev/CN=localhost"

    # Create extensions file for SAN
    cat > server.ext << EOF
authorityKeyIdentifier=keyid,issuer
basicConstraints=CA:FALSE
keyUsage = digitalSignature, nonRepudiation, keyEncipherment, dataEncipherment
subjectAltName = @alt_names

[alt_names]
DNS.1 = localhost
DNS.2 = chat-server
IP.1 = 127.0.0.1
IP.2 = ::1
EOF

    # Sign with CA
    openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key \
        -CAcreateserial -out server.crt -days 365 \
        -extfile server.ext

    # Cleanup
    rm -f server.csr server.ext
fi

echo "Certificates generated in $CERT_DIR"
echo "  - ca.crt (CA certificate)"
echo "  - server.crt (Server certificate)"
echo "  - server.key (Server private key)"
echo "  - dh2048.pem (DH parameters)"

