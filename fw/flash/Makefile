BDIR = /tmp/sol-flash

all: $(BDIR)/flash.gdbinit

$(BDIR):
	@mkdir -p $@

$(BDIR)/wenquanyi_9pt.bin: ../misc/bitmap_font/process.lua ../misc/bitmap_font/wenquanyi_9pt.bdf | $(BDIR)
	lua $(word 1, $^) < $(word 2, $^) > $@

../../cards/card_names: ../../cards/card_names.sh | $(BDIR)
	@mkdir -p $@
	bash ../../cards/card_names.sh $@

$(BDIR)/cards_all: ../../cards/all.c | $(BDIR)
	$(CC) $< -o $@ -O2 -lm

$(BDIR)/cards.bin: $(BDIR)/cards_all ../../cards/card_names | $(BDIR)
	cd ../../cards; $(BDIR)/cards_all > $@

$(BDIR)/gen: gen.c | $(BDIR)
	@mkdir -p $(dir $@)
	$(CC) $< -o $@ -O2 -lm

$(BDIR)/flash.gdbinit: $(BDIR)/gen $(BDIR)/wenquanyi_9pt.bin $(BDIR)/cards.bin | $(BDIR)
	$(BDIR)/gen $(BDIR)/wenquanyi_9pt.bin $(BDIR)/cards.bin $@

clean:
	rm -rf $(BDIR)/*

.PHONY: all clean
