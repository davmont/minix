cat << 'DIFF' > patch.diff
--- usr.sbin/makefs/cd9660.c
+++ usr.sbin/makefs/cd9660.c
@@ -174,6 +174,7 @@
 static cd9660node *cd9660_create_special_directory(iso9660_disk *, u_char,
     cd9660node *);
 static int  cd9660_add_generic_bootimage(iso9660_disk *, const char *);
+static void cd9660_add_padding_sectors(iso9660_disk *);


 /*
@@ -645,11 +646,7 @@
		    PRId64 "\n", __func__, diskStructure->totalSectors);
	}

-	/*
-	 * Add padding sectors at the end
-	 * TODO: Clean this up and separate padding
-	 */
-	if (diskStructure->include_padding_areas)
-		diskStructure->totalSectors += 150;
+	cd9660_add_padding_sectors(diskStructure);

	cd9660_write_image(diskStructure, image);

@@ -668,6 +665,13 @@

 /* Generic function pointer - implement later */
 typedef int (*cd9660node_func)(cd9660node *);
+
+static void
+cd9660_add_padding_sectors(iso9660_disk *diskStructure)
+{
+	if (diskStructure->include_padding_areas)
+		diskStructure->totalSectors += CD9660_END_PADDING;
+}

 static void
 cd9660_finalize_PVD(iso9660_disk *diskStructure)
DIFF
patch -p0 < patch.diff
