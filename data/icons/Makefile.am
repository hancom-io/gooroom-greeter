EXTRA_DIST =	\
	$(actions_icon_DATA)

actions_icondir = $(datadir)/icons/hicolor/scalable/actions

actions_icon_DATA =	\
	gooroom-greeter-hibernate-symbolic.svg	\
	gooroom-greeter-restart-symbolic.svg	\
	gooroom-greeter-shutdown-symbolic.svg	\
	gooroom-greeter-suspend-symbolic.svg

gtk_update_icon_cache = gtk-update-icon-cache -f -t $(datadir)/icons/hicolor

install-data-hook: update-icon-cache
uninstall-hook: update-icon-cache
update-icon-cache:
	@-if test -z "$(DESTDIR)"; then \
		echo "Updating Gtk icon cache."; \
		$(gtk_update_icon_cache); \
	else \
		echo "*** Icon cache not updated.  After (un)install, run this:"; \
		echo "***   $(gtk_update_icon_cache)"; \
	fi
