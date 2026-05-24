#!/usr/bin/env bash

# Build OCI and classic Docker images in a single Docker build pass.
# Also extracts the binary from the image rootfs.
# Usage: build.sh <PLATFORM> <SUFFIX> <MUSLCC_TAG> [VERSION] [BUILD_DIR]
set -euo pipefail

_usage='usage: build.sh <PLATFORM> <SUFFIX> <MUSLCC_TAG> [VERSION] [BUILD_DIR]'

: "${PLATFORM:=${1:?$_usage}}"
: "${SUFFIX:=${2:?$_usage}}"
: "${MUSLCC_TAG:=${3:?$_usage}}"
: "${VERSION:=${4:-dev}}"
: "${BUILD_DIR:=${5:-build}}"
: "${IMAGE_NAME:=wg2awg}"
: "${TAG:=$IMAGE_NAME:$VERSION-$SUFFIX}"

work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT


docker buildx build \
    --platform "$PLATFORM" \
    --build-arg VERSION="$VERSION" \
    --build-arg MUSLCC_TAG="$MUSLCC_TAG" \
    --output "type=local,dest=$work_dir/rootfs" \
    .

# Extract binary from rootfs
cp "$work_dir/rootfs/wg2awg" "$BUILD_DIR/$IMAGE_NAME-$SUFFIX"
echo "Created $BUILD_DIR/$IMAGE_NAME-$SUFFIX (binary)"

created=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

# Shared layer tar
tar cf "$work_dir/layer.tar" -C "$work_dir/rootfs" .
layer_sha=$(sha256sum "$work_dir/layer.tar" | cut -d' ' -f1)
layer_size=$(wc -c < "$work_dir/layer.tar")

# Parse arch/variant from platform string (e.g. linux/arm/v7 -> arm, 7)
arch="${PLATFORM#linux/}"
variant=""
case "$arch" in
    arm/v*) variant="${arch##*/v}"; arch="arm" ;;
esac

# Image config (shared between OCI and Docker formats)
config=$(jq -cn \
    --arg arch    "$arch" \
    --arg variant "$variant" \
    --arg created "$created" \
    --arg layer   "sha256:$layer_sha" \
    '{
        architecture: $arch,
        os:           "linux",
        created:      $created,
        config:       { Entrypoint: ["/wg2awg"] },
        rootfs:       { type: "layers", diff_ids: [$layer] }
    } | if $variant != "" then . + {variant: ("v" + $variant)} else . end')
config_sha=$(printf '%s' "$config" | sha256sum | cut -d' ' -f1)
config_size=$(printf '%s' "$config" | wc -c)

# OCI Image format
mkdir -p "$work_dir/oci/blobs/sha256"
cp "$work_dir/layer.tar"   "$work_dir/oci/blobs/sha256/$layer_sha"
printf '%s' "$config" > "$work_dir/oci/blobs/sha256/$config_sha"

manifest=$(jq -cn \
    --arg  config_digest "sha256:$config_sha" \
    --argjson config_size "$config_size" \
    --arg  layer_digest  "sha256:$layer_sha" \
    --argjson layer_size  "$layer_size" \
    '{
        schemaVersion: 2,
        mediaType:     "application/vnd.oci.image.manifest.v1+json",
        config: {
            mediaType: "application/vnd.oci.image.config.v1+json",
            digest:    $config_digest,
            size:      $config_size
        },
        layers: [{
            mediaType: "application/vnd.oci.image.layer.v1.tar",
            digest:    $layer_digest,
            size:      $layer_size
        }]
    }')
manifest_sha=$(printf '%s' "$manifest" | sha256sum | cut -d' ' -f1)
manifest_size=$(printf '%s' "$manifest" | wc -c)
printf '%s' "$manifest" > "$work_dir/oci/blobs/sha256/$manifest_sha"

jq -cn \
    --arg  arch            "$arch" \
    --arg  variant         "$variant" \
    --arg  manifest_digest "sha256:$manifest_sha" \
    --argjson manifest_size "$manifest_size" \
    '{
        schemaVersion: 2,
        manifests: [{
            mediaType: "application/vnd.oci.image.manifest.v1+json",
            digest:    $manifest_digest,
            size:      $manifest_size,
            platform:  ({ os: "linux", architecture: $arch }
                        | if $variant != ""
                          then . + {variant: ("v" + $variant)}
                          else . end
                        )
        }]
    }' > "$work_dir/oci/index.json"

printf '{"imageLayoutVersion":"1.0.0"}' > "$work_dir/oci/oci-layout"

tar czf "$BUILD_DIR/$IMAGE_NAME-$SUFFIX-oci.tar.gz" -C "$work_dir/oci" .
echo "Created $BUILD_DIR/$IMAGE_NAME-$SUFFIX-oci.tar.gz (OCI)"

# Classic Docker format
mkdir -p "$work_dir/docker/$layer_sha"
cp "$work_dir/layer.tar"   "$work_dir/docker/$layer_sha/layer.tar"
printf '%s' "$config" > "$work_dir/docker/$config_sha.json"
printf '1.0'           > "$work_dir/docker/$layer_sha/VERSION"
jq -cn --arg id "$layer_sha" '{id: $id}' > "$work_dir/docker/$layer_sha/json"

jq -cn \
    --arg config  "$config_sha.json" \
    --arg tag     "$TAG" \
    --arg layer   "$layer_sha/layer.tar" \
    '[{ Config: $config, RepoTags: [$tag], Layers: [$layer] }]' \
    > "$work_dir/docker/manifest.json"

jq -cn \
    --arg repo "$IMAGE_NAME" \
    --arg rtag "$VERSION-$SUFFIX" \
    --arg sha  "$config_sha" \
    '{($repo): {($rtag): $sha}}' \
    > "$work_dir/docker/repositories"

tar czf "$BUILD_DIR/$IMAGE_NAME-$SUFFIX-docker.tar.gz" -C "$work_dir/docker" .
echo "Created $BUILD_DIR/$IMAGE_NAME-$SUFFIX-docker.tar.gz (Docker)"
