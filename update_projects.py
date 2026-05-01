import os
import re

def update_vcxproj_files(directory):
    for root, dirs, files in os.walk(directory):
        for file in files:
            if file.endswith('.vcxproj'):
                filepath = os.path.join(root, file)
                try:
                    with open(filepath, 'r', encoding='utf-8') as f:
                        content = f.read()
                    
                    original = content
                    content = content.replace('ToolsVersion="15.0"', 'ToolsVersion="Current"')
                    content = content.replace('PlatformToolset>v142</PlatformToolset>', 'PlatformToolset>v143</PlatformToolset>')
                    
                    if content != original:
                        with open(filepath, 'w', encoding='utf-8') as f:
                            f.write(content)
                        print(f'Updated: {filepath}')
                    else:
                        print(f'Skipped: {filepath}')
                except Exception as e:
                    print(f'Error processing {filepath}: {e}')

if __name__ == '__main__':
    # Update Plugin projects
    plugin_dir = r'q:\AppData\ydwe\YDWE\Development\Plugin'
    print('Updating Plugin projects...')
    update_vcxproj_files(plugin_dir)
    
    # Update OpenSource projects
    opensource_dir = r'q:\AppData\ydwe\YDWE\OpenSource'
    print('\nUpdating OpenSource projects...')
    update_vcxproj_files(opensource_dir)
    
    print('\nDone!')
