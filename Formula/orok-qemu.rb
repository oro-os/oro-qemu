# Formula for orok-qemu
#
# This file is a TEMPLATE. The version in this repo is never committed by CI.
# CI generates the real formula (with the correct bottle SHA256) and publishes
# it to GitHub Pages. Install from there:
#
#   brew install https://oro-os.github.io/oro-qemu/Formula/orok-qemu.rb
#
class OrokQemu < Formula
  desc "Oro OS fork of QEMU (orok-qemu-system-{x86_64,aarch64,riscv64})"
  homepage "https://github.com/oro-os/oro-qemu"
  version "10.2.50-orok"
  license "GPL-2.0-only"

  # ── Bottle (pre-built binary for macOS arm64) ──────────────────────────────
  # CI substitutes PLACEHOLDER_ROOT_URL and PLACEHOLDER_SHA before publishing.
  bottle do
    root_url "PLACEHOLDER_ROOT_URL"
    sha256 cellar: :any_skip_relocation,
           arm64_sequoia: "PLACEHOLDER_SHA"
  end

  depends_on "ninja" => :build
  depends_on "pkg-config" => :build
  depends_on "glib"

  # Source build (used when no bottle matches the current OS)
  head "https://github.com/oro-os/oro-qemu.git", branch: "master"

  def install
    system "./configure", "--prefix=#{prefix}"
    system "ninja", "-C", "build",
           "qemu-system-x86_64",
           "qemu-system-aarch64",
           "qemu-system-riscv64"
    bin.install "build/qemu-system-x86_64"  => "orok-qemu-system-x86_64"
    bin.install "build/qemu-system-aarch64" => "orok-qemu-system-aarch64"
    bin.install "build/qemu-system-riscv64" => "orok-qemu-system-riscv64"
  end

  test do
    assert_match "QEMU", shell_output("#{bin}/orok-qemu-system-x86_64 --version")
  end
end
