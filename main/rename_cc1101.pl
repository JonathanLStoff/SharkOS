#!/usr/bin/perl
use strict;
use warnings;

# Read the file
my $file = $ARGV[0] or die "Usage: $0 <file>\n";
open(my $fh, '<', $file) or die "Cannot read $file: $!\n";
my $content = do { local $/; <$fh> };
close($fh);

# --- Rename file-scope globals to per-instance member variables ---
# Process longer / more specific names first to avoid partial renamings

# Array variants first
$content =~ s/\bSCK_PIN_M\b/_SCK_PIN_M/g;
$content =~ s/\bMISO_PIN_M\b/_MISO_PIN_M/g;
$content =~ s/\bMOSI_PIN_M\b/_MOSI_PIN_M/g;
$content =~ s/\bSS_PIN_M\b/_SS_PIN_M/g;
$content =~ s/\bGDO0_M\b/_GDO0_M/g;
$content =~ s/\bGDO2_M\b/_GDO2_M/g;

# Scalar pin names
$content =~ s/\bSCK_PIN\b/_SCK_PIN/g;
$content =~ s/\bMISO_PIN\b/_MISO_PIN/g;
$content =~ s/\bMOSI_PIN\b/_MOSI_PIN/g;
$content =~ s/\bSS_PIN\b/_SS_PIN/g;
$content =~ s/\bGDO0\b/_GDO0/g;
$content =~ s/\bGDO2\b/_GDO2/g;
$content =~ s/\bgdo_set\b/_gdo_set/g;

# State vars
$content =~ s/\bspi_initialized\b/_spi_initialized/g;
$content =~ s/\blast_pa\b/_last_pa/g;
$content =~ s/\bm4RxBw\b/_m4RxBw/g;
$content =~ s/\bm4DaRa\b/_m4DaRa/g;
$content =~ s/\bm2DCOFF\b/_m2DCOFF/g;
$content =~ s/\bm2MODFM\b/_m2MODFM/g;
$content =~ s/\bm2MANCH\b/_m2MANCH/g;
$content =~ s/\bm2SYNCM\b/_m2SYNCM/g;
$content =~ s/\bm1FEC\b/_m1FEC/g;
$content =~ s/\bm1PRE\b/_m1PRE/g;
$content =~ s/\bm1CHSP\b/_m1CHSP/g;
$content =~ s/\bpc1PQT\b/_pc1PQT/g;
$content =~ s/\bpc1CRC_AF\b/_pc1CRC_AF/g;
$content =~ s/\bpc1APP_ST\b/_pc1APP_ST/g;
$content =~ s/\bpc1ADRCHK\b/_pc1ADRCHK/g;
$content =~ s/\bpc0WDATA\b/_pc0WDATA/g;
$content =~ s/\bpc0PktForm\b/_pc0PktForm/g;
$content =~ s/\bpc0CRC_EN\b/_pc0CRC_EN/g;
$content =~ s/\bpc0LenConf\b/_pc0LenConf/g;
$content =~ s/\btrxstate\b/_trxstate/g;
$content =~ s/\bfrend0\b/_frend0/g;
$content =~ s/\bccmode\b/_ccmode/g;
$content =~ s/\bclb1\b/_clb1/g;
$content =~ s/\bclb2\b/_clb2/g;
$content =~ s/\bclb3\b/_clb3/g;
$content =~ s/\bclb4\b/_clb4/g;

# PA_TABLE: only bare PA_TABLE, not PA_TABLE_315 / PA_TABLE_433 / etc.
$content =~ s/\bPA_TABLE(?!_)\b/_PA_TABLE/g;

# Short names (need word boundaries to avoid mangling identifiers)
$content =~ s/\bmodulation\b/_modulation/g;
$content =~ s/\bchan\b/_chan/g;
$content =~ s/\bMHz\b/_MHz/g;

# spi (the bool flag, not SPIClass) — only lowercase plain 'spi'
# Careful: must not rename 'SPI' (uppercase) — word boundary handles case
$content =~ s/\bspi\b/_spi/g;

# pa (the int power level, not part of larger words)
$content =~ s/\bpa\b/_pa/g;

# --- Replace SPI.xxx() with _spiBus->xxx() ---
$content =~ s/\bSPI\.begin\(/_spiBus->begin(/g;
$content =~ s/\bSPI\.transfer\(/_spiBus->transfer(/g;
$content =~ s/\bSPI\.endTransaction\(\)/_spiBus->endTransaction()/g;
$content =~ s/\bSPI\.beginTransaction\(/_spiBus->beginTransaction(/g;
$content =~ s/\bSPI\.end\(\)/_spiBus->end()/g;

# --- Fix Calibrate() which calls ELECHOUSE_cc1101.SpiReadStatus() on self ---
$content =~ s/ELECHOUSE_cc1101\.SpiReadStatus\(/SpiReadStatus(/g;

# Write back
open(my $out, '>', $file) or die "Cannot write $file: $!\n";
print $out $content;
close($out);

print "Done. Renamed all globals to instance members in $file\n";
