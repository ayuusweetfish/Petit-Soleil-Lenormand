from krita import *
from PyQt5.QtWidgets import QMessageBox
from PyQt5.QtCore import QRect

def message(s):
  b = QMessageBox()
  b.setText(s)
  b.exec_()

class CardHelper(Extension):

  def __init__(self, parent):
    super().__init__(parent)

  def setup(self):
    pass

  def createActions(self, window):
    action = window.createAction("card_helper_export", "Card helper export", "tools/scripts")
    action.triggered.connect(self.export)

  def export(self):
    doc = Krita.instance().activeDocument()
    if doc is None:
      message('No document open')
      return

    doc.scaleImage(200, 200, int(doc.xRes()), int(doc.yRes()), '')

    root = doc.rootNode()
    existingChildren = root.childNodes()

    merged_outlines = doc.createNode("Merged outlines", "groupLayer")
    root.addChildNode(merged_outlines, None)
    merged_shadows = doc.createNode("Merged shadows", "groupLayer")
    root.addChildNode(merged_shadows, None)

    for i, node in enumerate(existingChildren):
      if i == 0 and ('背景' in node.name().casefold() or 'background' in node.name().casefold()):
        clone = node.duplicate()
        clone.setVisible(True)
        merged_outlines.addChildNode(clone, None)
        merged_shadows.addChildNode(clone.duplicate(), None)
      elif node.visible():
        # Opacity is 8-bit integer; 20% corresponds to 51
        if node.opacity() >= 60:
          merged_outlines.addChildNode(node.duplicate(), None)
        elif node.opacity() < 60 and 'shadow' in node.name().casefold():
          clone = node.duplicate()
          clone.setOpacity(255)
          merged_shadows.addChildNode(clone, None)

    # Export
    Krita.instance().setBatchmode(True)

    card_id = os.path.basename(doc.fileName())[:2]
    export_path_base = os.path.dirname(doc.fileName()) + os.sep + card_id
    full_rect = QRect(0, 0, 200, 200)
    merged_outlines.save(export_path_base + '_outline.png', int(doc.xRes()), int(doc.yRes()), InfoObject(), full_rect)
    merged_shadows.save(export_path_base + '_shadow.png', int(doc.xRes()), int(doc.yRes()), InfoObject(), full_rect)
    message('Success!\nImages exported at ' + export_path_base + '_{shadow,outline}.png')

    merged_shadows.remove()
    merged_outlines.remove()

    Krita.instance().setBatchmode(False)

Krita.instance().addExtension(CardHelper(Krita.instance()))
