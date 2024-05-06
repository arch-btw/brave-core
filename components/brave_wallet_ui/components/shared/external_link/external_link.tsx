// Copyright (c) 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

import * as React from 'react'
import { ButtonProps } from '@brave/leo/react/button'
import Icon from '@brave/leo/react/icon'

// utils
import { openTab } from '../../../utils/routes-utils'

// styles
import { LinkButton } from './external_link.style'

interface Props extends ButtonProps<undefined, boolean> {
  width?: string
  children?: React.ReactNode
}

export const ExternalLink = (props: Props) => {
  const { text, href, ...rest } = props

  return (
    <LinkButton
      {...rest}
      onClick={() => openTab(href)}
    >
      {text}
      <div slot='icon-after'>
        <Icon name='launch' />
      </div>
    </LinkButton>
  )
}
