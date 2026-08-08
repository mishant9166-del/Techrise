import xml.etree.ElementTree as ET

tree = ET.parse('OBSBasic.ui')
root = tree.getroot()

# Find customActionToolbar
toolbar = None
main_widget = root.find('.//widget[@class="QMainWindow"]')
for child in list(main_widget):
    if child.tag == 'widget' and child.attrib.get('class') == 'QToolBar' and child.attrib.get('name') == 'customActionToolbar':
        toolbar = child
        break

if toolbar is not None:
    # Remove attributes
    to_remove = []
    for attr in toolbar.findall('attribute'):
        to_remove.append(attr)
    for attr in to_remove:
        toolbar.remove(attr)
        
    main_widget.remove(toolbar)
    
    # Find scenesFrame layout
    scenes_layout = root.find('.//widget[@name="scenesFrame"]/layout')
    if scenes_layout is not None:
        item = ET.Element('item')
        item.append(toolbar)
        # Insert at the top (index 5 because of 5 properties)
        # Let's find the first item
        idx = 0
        for i, child in enumerate(list(scenes_layout)):
            if child.tag == 'item':
                idx = i
                break
        scenes_layout.insert(idx, item)
        tree.write('OBSBasic.ui', encoding='utf-8', xml_declaration=True)
        print('Moved toolbar successfully.')
    else:
        print('Could not find scenes_layout.')
else:
    print('Could not find customActionToolbar.')
