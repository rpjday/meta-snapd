SUMMARY = "The snapd and snap tools enable systems to work with .snap files."
HOMEPAGE = "https://www.snapcraft.io"
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://${WORKDIR}/${PN}-${PV}/COPYING;md5=d32239bcb673463ab874e80d47fae504"

SRC_URI = "	\
	https://${GO_IMPORT}/releases/download/${PV}/snapd_${PV}.vendor.tar.xz	\
	file://0001-mkversion-data-generate-supported-assert-formats-inf.patch \
"

PACKAGECONFIG ??= "${@bb.utils.contains('DISTRO_FEATURES', 'apparmor', 'apparmor', '', d)}"
PACKAGECONFIG[apparmor] = "--enable-apparmor,--disable-apparmor,apparmor,apparmor"

GO_IMPORT = "github.com/snapcore/snapd"

DEPENDS += " \
	glib-2.0		\
	libcap			\
	libseccomp		\
	udev			\
	xfsprogs		\
"

RDEPENDS:${PN} += " \
	bash			\
	ca-certificates		\
	kernel-module-squashfs	\
	squashfs-tools		\
"

S = "${WORKDIR}/${PN}-${PV}"

EXTRA_OECONF += "			\
	--libexecdir=${libdir}/snapd	\
	--with-snap-mount-dir=/snap \
"

inherit systemd autotools pkgconfig go

# disable shared runtime for x86
# https://forum.snapcraft.io/t/yocto-rocko-core-snap-panic/3261
# GO_DYNLINK is set with arch overrides in goarch.bbclass
GO_DYNLINK:x86 = ""
GO_DYNLINK:x86-64 = ""
GO_DYNLINK:arm = ""
GO_DYNLINK:aarch64 = ""

# Our tools build with autotools are inside the cmd subdirectory
# and we need to tell the autotools class to look in there.
AUTOTOOLS_SCRIPT_PATH = "${S}/cmd"

SYSTEMD_SERVICE:${PN} = "snapd.service"

GO_BUILD_TAGS_snapd = "nosecboot"
GO_BUILD_TAGS = "nosecboot nomanagers"

# The go class does export a do_configure function, of which we need
# to change the symlink set-up, to target snapd's environment.
do_configure() {
	mkdir -p ${S}/src/github.com/snapcore
	ln -snf ${S} ${S}/src/${GO_IMPORT}
	go_do_configure
	# internally calls go run to generate some assets
	(cd ${S} ; GOARCH=${GOHOSTARCH} sh -x ./mkversion.sh ${PV})
	autotools_do_configure
}

do_compile() {
	(
		cd ${S}
		${GO} install -tags '${GO_BUILD_TAGS_snapd}' -mod=vendor ${GOBUILDFLAGS} github.com/snapcore/snapd/cmd/snapd
		${GO} install -tags '${GO_BUILD_TAGS}' -mod=vendor ${GOBUILDFLAGS} github.com/snapcore/snapd/cmd/snap
		${GO} install -tags '${GO_BUILD_TAGS}' -mod=vendor ${GOBUILDFLAGS} github.com/snapcore/snapd/cmd/snap-seccomp
		${GO} install -tags '${GO_BUILD_TAGS}' -mod=vendor ${GOBUILDFLAGS} github.com/snapcore/snapd/cmd/snap-failure

		# these *must* be built statically
		for prog in snap-exec snap-update-ns snapctl; do
			${GO} install -tags '${GO_BUILD_TAGS}' -mod=vendor -v \
			      -ldflags="${GO_RPATH} -linkmode=external -extldflags '${HOST_CC_ARCH}${TOOLCHAIN_OPTIONS} ${GO_RPATH_LINK} ${LDFLAGS} -static'" \
			      github.com/snapcore/snapd/cmd/$prog
		done
	)
	# build the rest
	autotools_do_compile
}

do_install() {
	install -d ${D}${libdir}/snapd
	install -d ${D}${bindir}
	install -d ${D}${systemd_unitdir}/system
	install -d ${D}/var/lib/snapd
	install -d ${D}/var/lib/snapd/snaps
	install -d ${D}/var/lib/snapd/lib/gl
	install -d ${D}/var/lib/snapd/desktop
	install -d ${D}/var/lib/snapd/environment
	install -d ${D}/var/snap
	install -d ${D}${sysconfdir}/profile.d
	install -d ${D}${systemd_unitdir}/system-generators

	oe_runmake -C ${B} install DESTDIR=${D}
	oe_runmake -C ${S}/data/systemd install \
		DESTDIR=${D} \
		BINDIR=${bindir} \
		LIBEXECDIR=${libdir} \
		SYSTEMDSYSTEMUNITDIR=${systemd_system_unitdir} \
		SNAP_MOUNT_DIR=/snap \
		SNAPD_ENVIRONMENT_FILE=${sysconfdir}/default/snapd

	# systemd system-environment-generators directory is not handled with a
	# varaible in systemd.pc so the build code does an educated guess of using
	# ${prefix}/lib/systemd/system-environment-generators which ends up as
	# /usr/lib/systemd/.., but we want /lib/systemd/..
	cp -av ${D}${prefix}${systemd_unitdir}/system-environment-generators \
	   ${D}${systemd_unitdir}
	rm -rf ${D}${prefix}${systemd_unitdir}

	install -m 0755 ${B}/${GO_BUILD_BINDIR}/snapd ${D}${libdir}/snapd/
	install -m 0755 ${B}/${GO_BUILD_BINDIR}/snap-exec ${D}${libdir}/snapd/
	install -m 0755 ${B}/${GO_BUILD_BINDIR}/snap-seccomp ${D}${libdir}/snapd/
	install -m 0755 ${B}/${GO_BUILD_BINDIR}/snap-update-ns ${D}${libdir}/snapd/
	install -m 0755 ${B}/${GO_BUILD_BINDIR}/snapctl ${D}${libdir}/snapd/
	install -m 0755 ${B}/${GO_BUILD_BINDIR}/snap-failure ${D}${libdir}/snapd/
	install -m 0755 ${B}/${GO_BUILD_BINDIR}/snap ${D}${bindir}
	ln -s ${libdir}/snapd/snapctl ${D}${bindir}/snapctl

	echo "PATH=\$PATH:/snap/bin" > ${D}${sysconfdir}/profile.d/20-snap.sh

	# ubuntu-core-launcher is dead
	rm -fv ${D}${bindir}/ubuntu-core-launcher
	# drop unnecessary units
	rm -fv ${D}${systemd_unitdir}/system/snapd.system-shutdown.service
	rm -fv ${D}${systemd_unitdir}/system/snapd.snap-repair.*
	rm -fv ${D}${systemd_unitdir}/system/snapd.core-fixup.*
	rm -fv ${D}${systemd_unitdir}/snapd.recovery-chooser-trigger.service
	# and related scripts and binaries
	rm -fv ${D}${libdir}/snapd/snapd.core-fixup.sh
	rm -fv ${D}${libdir}/snapd/system-shutdown
}

FILES:${PN} += "                                    \
	${systemd_unitdir}/system/                        \
	${systemd_unitdir}/system-generators/             \
	${systemd_unitdir}/system-environment-generators/	\
	/var/lib/snapd                                    \
	/var/snap                                         \
	${baselib}/udev/snappy-app-dev                    \
"
