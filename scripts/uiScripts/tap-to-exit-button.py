"""Button program for ring setup.
	
   This button stops running when it is pressed."""


from PyQt5 import uic, QtWidgets, QtCore, QtGui
from sys import exit, argv
import argparse
import io

ui = """<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>uiButtonScreen</class>
 <widget class="QWidget" name="uiButtonScreen">
  <property name="geometry">
   <rect>
    <x>0</x>
    <y>0</y>
    <width>800</width>
    <height>480</height>
   </rect>
  </property>
  <property name="windowTitle">
   <string notr="true">Chronos Screen</string>
  </property>
  <widget class="QPushButton" name="uiSetButton">
   <property name="geometry">
    <rect>
     <x>0</x>
     <y>0</y>
     <width>800</width>
     <height>480</height>
    </rect>
   </property>
   <property name="font">
    <font>
     <pointsize>50</pointsize>
    </font>
   </property>
   <property name="focusPolicy">
    <enum>Qt::NoFocus</enum>
   </property>
   <property name="styleSheet">
    <string notr="true">background: rgba(127,127,127,64)</string>
   </property>
   <property name="text">
    <string>Set #</string>
   </property>
  </widget>
 </widget>
 <resources/>
 <connections/>
</ui>
"""


class ButtonScreen(QtWidgets.QWidget):
    def __init__(self):
        super().__init__()
        
        uic.loadUi(io.StringIO(ui), self)
        
        # Panel init.
        self.move(0, 0)
        self.setWindowFlags(QtCore.Qt.FramelessWindowHint)
        self.setAttribute(QtCore.Qt.WA_TranslucentBackground, True)
        
        #First (and only) arg: Set initial button text.
        parser = argparse.ArgumentParser(description="Full screen button")
        parser.add_argument('--background', default='00000000', type=str, nargs='?', help="background in hex format (7F7F7FFF would be solid grey)")
        parser.add_argument('--foreground', default='FFFFFFFF', type=str, nargs='?', help="foreground in hex format (7F7F7FFF would be solid grey)")
        parser.add_argument('--size',       default=50, type=int, nargs='?', help="size of font")
        parser.add_argument('text',         default='', type=str, nargs='*', help="text to be displayed")
        args = parser.parse_args()
        
        text = ' '.join(args.text)
        self.uiSetButton.setText(text)

        self.uiSetButton.setFont(QtGui.QFont('', args.size))
                
        if len(args.background) < 8:
            raise ValueError('background must be hex encoded RGBA')
        bgRed   = int(args.background[0:2], 16)
        bgGreen = int(args.background[2:4], 16)
        bgBlue  = int(args.background[4:6], 16)
        bgAlpha = int(args.background[6:8], 16)
        if len(args.foreground) < 8:
            raise ValueError('foreground must be hex encoded RGBA')
        fgRed   = int(args.foreground[0:2], 16)
        fgGreen = int(args.foreground[2:4], 16)
        fgBlue  = int(args.foreground[4:6], 16)
        fgAlpha = int(args.foreground[6:8], 16)
        self.uiSetButton.setStyleSheet("background: rgba(%d,%d,%d,%d); color: rgba(%d,%d,%d,%d);" % (bgRed, bgGreen, bgBlue, bgAlpha, fgRed, fgGreen, fgBlue, fgAlpha))

        # When the button is pressed, simply exit.
        self.uiSetButton.clicked.connect(exit)


if __name__ == '__main__':
    app = QtWidgets.QApplication(argv)
    
    buttonScreen = ButtonScreen()
    buttonScreen.show()
    
    exit(app.exec_())
