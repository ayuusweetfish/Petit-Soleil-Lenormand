BDIR ?= /tmp/sol-flash

all: $(BDIR)/flash.gdbinit

$(BDIR):
	@mkdir -p $@

$(BDIR)/wenquanyi_9pt.bin: ../misc/bitmap_font/process.lua ../misc/bitmap_font/wenquanyi_9pt.bdf | $(BDIR)
	lua $(word 1, $^) < $(word 2, $^) > $@

$(BDIR)/card_names: ../../cards/card_names.sh | $(BDIR)
	@mkdir -p $@
	bash ../../cards/card_names.sh $@

$(BDIR)/cards_all: ../../cards/all.c | $(BDIR)
	$(CC) $< -o $@ -lm

$(BDIR)/cards.bin: $(BDIR)/cards_all $(BDIR)/card_names ../../cards/card_directions.txt ../../cards/card_commentary.txt $(wildcard ../../cards/card_illustrations/%.png) | $(BDIR)
	$(word 1, $^) ../../cards $(word 2, $^) > $@

$(BDIR)/gen: gen.c | $(BDIR)
	$(CC) $< -o $@ -lm

$(BDIR)/flash.gdbinit: $(BDIR)/gen $(BDIR)/wenquanyi_9pt.bin $(BDIR)/cards.bin | $(BDIR)
	$(BDIR)/gen $(BDIR)/wenquanyi_9pt.bin $(BDIR)/cards.bin $@

clean:
	rm -rf $(BDIR)/*

.PHONY: all clean
