"""Button program for ring setup.
	
	This button stops running when it is pressed."""


from PyQt5 import uic, QtWidgets, QtCore
from sys import exit, argv

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
     <pointsize>200</pointsize>
    </font>
   </property>
   <property name="focusPolicy">
    <enum>Qt::NoFocus</enum>
   </property>
   <property name="styleSheet">
    <string notr="true">background: rgba(255,255,255,64)</string>
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
		if argv[1:2]:
			self.uiSetButton.setText(argv[1])
		
		# When the button is pressed, simply exit.
		self.uiSetButton.clicked.connect(exit)


if __name__ == '__main__':
	app = QtWidgets.QApplication(argv)
	
	buttonScreen = ButtonScreen()
	buttonScreen.show()
	
	exit(app.exec_())
